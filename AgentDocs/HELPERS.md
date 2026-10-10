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

## Mandatory tooltip rule

- Every Mixtormat tooltip must be themed. Prefer `SMixtormatHelp` around controls.
- For input widgets that cannot be wrapped without altering drag/focus behavior, use `SMixtormatHelp::MakeStyledToolTip` with `SetToolTip`.
- No raw `.ToolTipText(...)`, `SetToolTipText(...)` or unstyled `SToolTip` in new code. Existing use at call sites of `SMixtormatGroupAction` is routed through its styled wrapper, but should not be expanded.
- The shared help surface owns typography, palette, compositing, padding and width. Never duplicate these recipes at individual call sites.
- Preserve mouse capture, focus, modifiers, hover/click semantics and localized help strings.

## Mechanisms

| Mechanism | Where | Notes |
|---|---|---|
| `SMixtormatHelp` | `UI/Menus/SMixtormatHelp.h` | Styled hover-only help host (menu anchor); opens after `MixtormatTokens::HelpDelay` (0.35 seconds / 350 ms). Used by shell actions, sliders, the icon rail, preview quick controls, and overlay resize/fit controls. It clears native tooltips in its child subtree. |
| Legacy native tooltips | `.ToolTipText(...)` / `SetToolTipText(...)` | Forbidden for new Mixtormat code. Migrate existing sites to styled help; do not repeat the native pattern. |
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

Workspace layout: Layers/Library/Global are the normal left-column rail/pages. Layers can
pop out and return by rail click or snap-back drag. The gallery is one MATERIALS/MASKS
bottom drawer over the whole workspace. `L` reads Home → Popped Out → Hidden; `G`
opens/closes the drawer. See the overlay-workspace handoff.

| Key | Action | Advertised where |
|---|---|---|
| `L` | Cycle the layer stack's placement: Docked → Overlay → Hidden → Docked. The left column's icon rail stays docked; only the stack travels | Top-bar tooltip `"Cycle Layers placement: Docked → Overlay → Hidden → Docked (L)."`; the control's label reads `Layers: Docked` / `Overlay` / `Hidden` |
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
| `Q` (bare) | Toggle viewport quick controls (render top, lighting left, geometry right, Actions placeholder bottom). Q again, Escape, an outside click, or choosing a mesh/light preset dismisses. Opening remains viewport-scoped; Q closes while the popup has focus. **Implemented, needs in-editor verification** — opening is routed through `FMixtormatPreviewViewportClient::InputKey`; closing while a control has focus uses the workspace input processor. Not Tab: Slate navigates focus on Tab regardless of a widget handling it, so Tab also lit up the top bar (I13). Archived delivery history: `AgentDocs/old_docs/viewport-quick-controls-plan.md` |

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
| Overlay Fit height | Bottom-centre chevron help | `SMixtormat_Overlays.cpp` | Done — `SMixtormatHelp`, shown only when the height is explicit; same control for both panels |
| Overlay header drag | None | `SMixtormat_Shell.cpp` | Inspector: identity row (no hint). Left panel: empty grab margin above the tab strip (no hint); the grab-hand cursor is the only affordance |
| `Tab` marking menu | None | planned | TODO — arrives with the viewport quick-controls plan; decide its own help surface |
| GLOBAL Preview/Viewport toggles | GLOBAL group rows | `SMixtormat_Shell.cpp` | Done — five switches with labels and tooltips; the settings below them are the viewport's own builders |
| `(L)` left-panel cycle | Top-bar control label + tooltip | `SMixtormat_Shell.cpp` | Done — the cycle moves the layer stack; the rail stays docked |
| Left column rail | Icon help | `SMixtormatIconRail.cpp` | Done — LAYERS / LIBRARY / GLOBAL use `SMixtormatHelp` for their tab hints |

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
- **Viewport shortcuts lack a consolidated help surface.** The implemented `Q` marking menu
  could also advertise `F`/`H`/`Z`/`V`; that help extension remains a proposal.
  Its delivery history is archived in `AgentDocs/old_docs/viewport-quick-controls-plan.md`.
- **`Tab` remains Slate focus navigation.** The marking menu uses `Q`, not Tab.
- **Temporary keys.** `U`/`M` module preview says "Temporary" in source.
  Preserve them in this work; removal needs explicit approval.
- **Empty-state copy** lives inline per panel; if a help pass comes, collect
  the strings in one table so tone stays consistent.

## Related

- `UI.md` — regions and widgets. `ICONS.md` — glyphs. `CONVENTIONS.md` —
  `LOCTEXT` rules. `auditdocs/workspace-layout/` — why `L`/`P` exist, and the
  viewport quick-controls plan for the planned `Tab` marking menu.
