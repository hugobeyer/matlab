# Helpers — tooltips, hints, and overlay help text

Initial draft. Source wins; verify before acting. This is the inventory of
everything that teaches the user **in context**: tooltips, shortcut suffixes,
menu shortcut strings, status-bar text, mode labels, and empty-state lines —
plus the TODO lists for mapping and architecture work on top of it.

## Conventions (extract of what the codebase already does)

- Tooltips name the key as a sentence suffix: `"Collapse or expand the material
  and mask galleries (G)."` — action first, key in parentheses, period last.
- Menus advertise keys with `.Shortcut(LOCTEXT(..., "F2"))` on the row.
- Keys **without a surface** (viewport hotkeys) are announced through
  `WorkingStatusText` — "the only indication, same as every other viewport
  hotkey here" (`SMixtormatPreviewViewport.cpp`).
- Aliases are not advertised: `F12` works but menus say `F2`; `Ctrl+Shift+Z`
  is named in the Redo tooltip only because it is a second redo path.
- Rename never lives on double-click (double-click opens/shuts a row); it is
  F2/F12 and the context menu only.
- Text lives in `LOCTEXT`/`NSLOCTEXT`, namespace `SMixtormat` for editor code.

## Mechanisms

| Mechanism | Where | Notes |
|---|---|---|
| `SMixtormatHelp` | `UI/Menus/SMixtormatHelp.h` | Hover-only help host (menu anchor); opens after `MixtormatTokens::HelpDelay` (theme `--help-delay`, 350 ms). Used by shell actions such as the top bar and group buttons. |
| Plain tooltips | `.ToolTipText(...)` on widgets | Everywhere else (sliders, buttons, rows). |
| Menu shortcut column | `Menu.Item(...).Shortcut(...)` | `Widgets/Layers/MixtormatLayerMenus.cpp` etc. |
| Status bar | `WorkingStatusText` | "Unsaved changes" / "All changes saved" / `Preview: <mode>`. Sole feedback for viewport hotkeys. |
| Mode label | `GetPreviewModeLabel()` | `"{0}  (Shift+V for Material)"` on the preview. |
| Empty states | Text blocks in panels | e.g. "No global variables yet.", "No layer selected". |
| Entry-commit rules | `UI/Controls/MixtormatEntryCommit.h` | Enter/Tab/click-away accept; Escape/right-click cancel — documented in the header. |

## Hotkey catalog — workspace (`SMixtormat`)

`L`/`P` fire workspace-wide through the Slate input preprocessor
(`SMixtormat.cpp::Construct`); the rest go through `OnKeyDown`, so they need
workspace focus. Text entry always wins.

| Key | Action | Advertised where |
|---|---|---|
| `L` | Collapse/expand Layers/Library | Top-bar tooltip `"… (L)."` |
| `P` | Collapse/expand Inspector *(becomes the Docked → Overlay → Hidden cycle — tooltip must change with it)* | Top-bar tooltip `"… (P)."` |
| `G` | Collapse/expand bottom galleries | Gallery collapse tooltip `"… (G)."` |
| `I` | Toggle region-ID preview for the selected child | Not advertised — no tooltip found |
| `F2` / `F12` | Rename selected layer or group | Menus `.Shortcut("F2")`; F12 undocumented alias |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo (tooltip also names `Ctrl+Shift+Z`) | Top-bar tooltips |
| `Backspace` | Reset hovered numeric control | Slider tooltip (see below) |

## Hotkey catalog — viewport (`FMixtormatPreviewViewportClient::InputKey`)

Focus = the viewport. Feedback = status bar only; there is no toolbar for
these yet.

| Key | Action |
|---|---|
| `F` | Frame the mesh (focus camera) — same meaning as elsewhere in the editor |
| `Space` or `H` | Toggle viewport overlay UI |
| `Z` (bare) | Toggle displacement — bare only, so `Ctrl+Z` still reaches undo |
| `V` | Cycle channel preview (Normal / RAM / Height / …) |
| `Shift+V` | Jump straight back to Material (also in the mode label) |
| `U` or `M` | Cycle module preview — **marked Temporary in source** |
| Mouse wheel | Zoom camera |

## Hotkey catalog — controls

| Surface | Gestures / keys |
|---|---|
| Slider | Tooltip: `"Drag to adjust · click to type · Shift fine · Ctrl+Shift finer · Ctrl snap · MMB or hover + Backspace to reset"` |
| Text entry (rename, slider type-in) | Enter / Tab / click-away commit; Escape or right-click cancels |
| Ramp editor | `F` frames the view (`SMixtormatRampEditor.cpp`) |
| Layer row | Double-click opens/shuts — never renames |

## TODOs — mapping

Goal: one row per hint so text, key, and callsite cannot drift. Fill this
table in as surfaces land; add a "Help text" section to the mandated
`Docs/UNREAL_STYLE_MAPPING.md` (rewrite plan §81) when that document exists.

| Hint / key | Surface | File | Status |
|---|---|---|---|
| `(L)`, `(P)` panel toggles | Top-bar tooltips | `SMixtormat_Shell.cpp` | Done — update `(P)` when the cycle ships |
| `(G)` galleries | Gallery collapse button | `SMixtormat_Shell.cpp` | Done |
| `(Ctrl+Z)` / `(Ctrl+Y or Ctrl+Shift+Z)` | Top-bar tooltips | `SMixtormat_Shell.cpp` | Done |
| `(F2)` rename | Context menus | `MixtormatLayerMenus.cpp` | Done |
| Slider gesture line | Slider tooltip | `SMixtormatSlider.cpp` | Done |
| `(Shift+V for Material)` | Preview mode label | `SMixtormatPreviewViewport.cpp` | Done |
| Viewport keys (F, H/Space, Z, V, U/M) | Status bar only | `SMixtormatPreviewViewport.cpp` | TODO — decide a discoverable surface |
| `I` region-ID preview | None | `SMixtormat.cpp` | TODO — add tooltip or menu entry |
| `HelpDelay` token | `SMixtormatHelp` | `MixtormatDesignTokens.h`, theme schema | Verify the schema entry and `--help-delay` mapping |
| Placement control `(P)` cycle | Inspector overlay header | `SMixtormat_Inspector.cpp` | TODO — arrives with the overlay handoff |

## TODOs — architecture

- **Single source for key strings.** A key currently lives in three places
  (tooltip text, menu `.Shortcut`, handler). Consider one small shortcut
  table feeding tooltips and the menus; at minimum grep for the old text when
  a binding changes.
- **Workspace keys are split.** `L`/`P` work everywhere (input preprocessor);
  `G`/`I`/`F2` need workspace focus. Decide whether all workspace-level keys
  move to the preprocessor, or none.
- **No contextual help overlay.** "Overlay text with context" today means
  tooltips plus the status bar. If a hover/help overlay is wanted (e.g. a
  viewport-key cheat sheet), extend `SMixtormatHelp` rather than inventing a
  second tooltip system.
- **Temporary keys.** `U`/`M` module preview says "Temporary" in source —
  either surface it properly or remove it.
- **Empty-state copy** lives inline per panel; if a help pass comes, collect
  the strings in one table so tone stays consistent.

## Related

- `UI.md` — regions and widgets. `ICONS.md` — glyphs. `CONVENTIONS.md` —
  `LOCTEXT` rules. `auditdocs/workspace-layout/` — why `L`/`P` exist.
