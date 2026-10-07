# Helpers — tooltips, hints, and overlay help text

Initial draft. Source wins; verify before acting. This is the inventory of
everything that teaches the user **in context**: tooltips, shortcut suffixes,
menu shortcut strings, status-bar text, mode labels, and empty-state lines —
plus the TODO lists for mapping and architecture work on top of it.

## Conventions (extract of what the codebase already does)

- Tooltips name the key as a sentence suffix: `"Collapse or expand the material
  and mask galleries (G)."` — action first, key in parentheses, period last.
- Menus advertise keys with `.Shortcut(LOCTEXT(..., "F2"))` on the row.
- Viewport feedback is not universally status-only: F moves the camera, H/Space
  changes overlay visibility, Z updates displacement and V changes the mode label
  as well as `WorkingStatusText` (`SMixtormat_Preview.cpp` L1400–1514). Some source
  comments still describe status-only feedback; trust the live bindings.
- Aliases are not advertised: `F12` works but menus say `F2`; `Ctrl+Shift+Z`
  is named in the Redo tooltip only because it is a second redo path.
- Rename never lives on double-click (double-click opens/shuts a row); it is
  F2/F12 and the context menu only.
- Text lives in `LOCTEXT`/`NSLOCTEXT`, namespace `SMixtormat` for editor code.

## Mechanisms

| Mechanism | Where | Notes |
|---|---|---|
| `SMixtormatHelp` | `UI/Menus/SMixtormatHelp.h` | Hover-only help host (menu anchor); opens after `MixtormatTokens::HelpDelay` (0.35 seconds / 350 ms). Token-only: no live-theme delay field/schema entry was found. Used by shell actions such as the top bar and group buttons. |
| Plain tooltips | `.ToolTipText(...)` on widgets | Everywhere else (sliders, buttons, rows). |
| Menu shortcut column | `Menu.Item(...).Shortcut(...)` | `Widgets/Layers/MixtormatLayerMenus.cpp` etc. |
| Status bar | `WorkingStatusText` | "Unsaved changes" / "All changes saved" / `Preview: <mode>`. Supplements visual/control feedback; not updated by every viewport hotkey. |
| Mode label | `GetPreviewModeLabel()` | `"{0}  (Shift+V for Material)"` on the preview. |
| Empty states | Text blocks in panels | e.g. "No global variables yet.", "No layer selected". |
| Entry-commit rules | `UI/Controls/MixtormatEntryCommit.h` | Enter/Tab/click-away accept; Escape/right-click cancel — documented in the header. |

## Hotkey catalog — workspace (`SMixtormat`)

`L`/`P` fire workspace-wide through the Slate input preprocessor
(`SMixtormat.cpp::Construct`); the rest go through `OnKeyDown`, so they need
workspace focus. The preprocessor yields for focused-widget type names containing
`EditableText` or `TextEntry`; that is a type check, not a general text-input proof.
It has no workspace/tab/window ownership guard (`SMixtormat.cpp` L30–48); verify
unrelated editor windows and modal/popup focus before extending it.

| Key | Action | Advertised where |
|---|---|---|
| `L` | Collapse/expand the left Layers/Library/GLOBAL panel *(proposed cycle for D22: confirm O8 and update help before changing behaviour)* | Top-bar tooltip `"… (L)."` |
| `P` | Cycle Inspector placement: Docked → Overlay → Hidden | Top-bar tooltip `"Cycle Inspector placement: Docked → Overlay → Hidden → Docked (P)."`; the control's own label reads `Inspector: Docked` / `Overlay` / `Hidden` |
| `G` | Collapse/expand bottom galleries | Gallery collapse tooltip `"… (G)."` |
| `I` | Toggle region-ID preview for the selected child | Not advertised — no tooltip found |
| `F2` / `F12` | Rename selected layer or group | Menus `.Shortcut("F2")`; F12 undocumented alias |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo (tooltip also names `Ctrl+Shift+Z`) | Top-bar tooltips |
| `Backspace` | Reset hovered numeric control | Slider tooltip (see below) |

## Hotkey catalog — viewport (`FMixtormatPreviewViewportClient::InputKey`)

Focus = the viewport. Feedback varies by action: camera/overlay changes, existing
controls, the mode label and/or status bar. Shortcut discoverability is incomplete.

| Key | Action |
|---|---|
| `F` | Frame the mesh (focus camera) — same meaning as elsewhere in the editor |
| `Space` or `H` | Toggle viewport overlay UI |
| `Z` (bare) | Toggle displacement — bare only, so `Ctrl+Z` still reaches undo |
| `V` | Cycle channel preview (Normal / RAM / Height / …) |
| `Shift+V` | Jump straight back to Material (also in the mode label) |
| `U` or `M` | Cycle module preview — **marked Temporary in source** |
| Mouse wheel | Zoom camera |
| `Tab` (bare) | **Planned, not implemented** — open the viewport marking menu. Belongs here (viewport focus), not in the workspace preprocessor, because Slate uses Tab for focus navigation. See `auditdocs/workspace-layout/viewport-quick-controls-plan.md` |

## Hotkey catalog — controls

| Surface | Gestures / keys |
|---|---|
| Slider | Tooltip: `"Drag to adjust · click to type · Shift fine · Ctrl+Shift finer · Ctrl snap · MMB or hover + Backspace to reset"` |
| Text entry (rename, slider type-in) | Enter / Tab / click-away commit; Escape or right-click cancels |
| Ramp editor | `F` frames the view (`SMixtormatRampEditor.cpp`) |
| Layer row | Double-click opens/shuts — never renames |
| Inspector overlay | Header row drags the panel; each corner resizes it (hover shows a small L outline, opposite corner fixed). Corner tooltip: `"Drag to resize the Inspector."` |

## TODOs — mapping

Goal: one row per hint so text, key, and callsite cannot drift. Fill this
table in as surfaces land; add a "Help text" section to the mandated
`Docs/UNREAL_STYLE_MAPPING.md` (rewrite plan §81) when that document exists.

| Hint / key | Surface | File | Status |
|---|---|---|---|
| `(L)`, `(P)` panel toggles | Top-bar tooltips | `SMixtormat_Shell.cpp` | Done — `(P)` now names the placement cycle |
| `(G)` galleries | Gallery collapse button | `SMixtormat_Shell.cpp` | Done |
| `(Ctrl+Z)` / `(Ctrl+Y or Ctrl+Shift+Z)` | Top-bar tooltips | `SMixtormat_Shell.cpp` | Done |
| `(F2)` rename | Context menus | `MixtormatLayerMenus.cpp` | Done |
| Slider gesture line | Slider tooltip | `SMixtormatSlider.cpp` | Done |
| `(Shift+V for Material)` | Preview mode label | `SMixtormatPreviewViewport.cpp` | Done |
| Viewport keys (F, H/Space, Z, V, U/M) | Mixed visual/control/status feedback | `SMixtormatPreviewViewport.cpp`, `SMixtormat_Preview.cpp` | TODO — decide a discoverable shortcut-help surface |
| `I` region-ID preview | None | `SMixtormat.cpp` | TODO — add tooltip or menu entry |
| `HelpDelay` token | `SMixtormatHelp` | `MixtormatDesignTokens.h` L468; `SMixtormatHelp.cpp` L58 | Token reader verified; no live-theme delay schema entry found |
| Placement control `(P)` cycle | Top-bar control label + tooltip | `SMixtormat_Shell.cpp` | Done — label reads the current placement |
| Overlay corner resize | Corner tooltip | `SMixtormat_Inspector.cpp` | Done — `"Drag to resize the Inspector."` |
| Overlay Fit height | Bottom-centre chevron tooltip | `SMixtormat_Overlays.cpp` | Done — shown only while the height is explicit; same control for both panels |
| Overlay header drag | None | `SMixtormat_Shell.cpp` | Inspector: identity row (no hint). Left panel: empty grab margin above the tab strip (no hint); the grab-hand cursor is the only affordance |
| `Tab` marking menu | None | planned | TODO — arrives with the viewport quick-controls plan; decide its own help surface |
| GLOBAL Preview/Viewport toggles | None | planned | TODO — group visibility switches need labels and tooltips |
| `(L)` left-panel cycle | Top-bar control label + tooltip | `SMixtormat_Shell.cpp` | TODO — arrives with the Layers placement model (D22) |

## TODOs — architecture

- **Single source for key strings.** A key currently lives in three places
  (tooltip text, menu `.Shortcut`, handler). Consider one small shortcut
  table feeding tooltips and the menus; at minimum grep for the old text when
  a binding changes.
- **Workspace keys are split.** `L`/`P` work everywhere (input preprocessor);
  `G`/`I`/`F2` need workspace focus. Decide whether all workspace-level keys
  move to the preprocessor, or none. `Tab` is the counter-example: it must stay
  viewport-scoped, so the split is likely permanent.
- **No contextual help overlay.** "Overlay text with context" today means
  tooltips plus the status bar. If a hover/help overlay is wanted (e.g. a
  viewport-key cheat sheet), extend `SMixtormatHelp` rather than inventing a
  second tooltip system.
- **Viewport shortcuts lack a consolidated help surface.** The planned `Tab` marking menu
  (`auditdocs/workspace-layout/viewport-quick-controls-plan.md`) is the candidate
  answer: it can list the viewport keys and the controls they act on, supplementing
  existing feedback. Decide whether the menu also advertises `F`/`H`/`Z`/`V`.
- **`Tab` collides with Slate focus navigation.** If the marking menu ships, bare
  Tab must be consumed only while the viewport has focus, and text entry must keep
  priority — the same rule the `L`/`P` preprocessor already follows.
- **Temporary keys.** `U`/`M` module preview says "Temporary" in source.
  Preserve them in this work; removal needs explicit approval.
- **Empty-state copy** lives inline per panel; if a help pass comes, collect
  the strings in one table so tone stays consistent.

## Related

- `UI.md` — regions and widgets. `ICONS.md` — glyphs. `CONVENTIONS.md` —
  `LOCTEXT` rules. `auditdocs/workspace-layout/` — why `L`/`P` exist, and the
  viewport quick-controls plan for the planned `Tab` marking menu.
