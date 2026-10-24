# Helpers — tooltips, hints, and overlay help text

Initial draft. Source wins; verify before acting. This is the inventory of
everything that teaches the user **in context**: tooltips, shortcut suffixes,
menu shortcut strings, status-bar text, mode labels, and empty-state lines —
plus the TODO lists for mapping and architecture work on top of it.

## Conventions (extract of what the codebase already does)

- Tooltips name the key as a sentence suffix: `"Collapse or expand the material
  and mask galleries (G)."` — action first, key in parentheses, period last.
- Menus advertise keys with `.Shortcut(LOCTEXT(..., "F2"))` on the row.
- V/W/E use transient top-center viewport feedback, not `WorkingStatusText`.
  Q mesh/light/reset choices, successful additions/output copies and shared-slider results
  reuse it. Repeated actions replace/restart the label; 1.4s hold + 0.5s fade. PreviewLabel,
  preview plate and padding are reused; H/Space hides optional feedback.
  Active slider, viewport-navigation and owned drag-target guidance hide the center label;
  changed slider values report once on release,
  capture loss or accepted type-in, never per mouse move. Existing status writes are preserved.
  F moves the camera and Z updates displacement; existing preview actions are preserved.
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
- `MakeStyledToolTip(Text, Preview)` can include an existing thumbnail widget above the text;
  mask-row help uses this instead of a separately styled tooltip. The adapter retains Slate's
  timing/lifecycle; the hover-only host uses `HelpDelay`.
- `SMixtormatIconButton.ToolTip(FText)` is styled; its inherited `.ToolTipText` is not.
  `SMixtormatLayerIcon` callers must use the styled `IToolTip` adapter. MenuBuilder now
  adapts nonempty help centrally; the group/shell-action text wrappers already do so.
- Preserve mouse capture, focus, modifiers, hover/click semantics and localized help strings.

## Mechanisms

| Mechanism | Where | Notes |
|---|---|---|
| `SMixtormatHelp` | `UI/Menus/SMixtormatHelp.h` | Styled hover-only help host (menu anchor); opens after `MixtormatTokens::HelpDelay` (0.35 seconds / 350 ms). Used by shell actions, sliders, the icon rail, preview quick controls, and overlay resize/fit controls. It clears native tooltips in its child subtree. |
| Legacy native tooltips | `.ToolTipText(...)` / `SetToolTipText(...)` | Forbidden for new Mixtormat code. Migrate existing sites to styled help; do not repeat the native pattern. |
| Menu shortcut column | `Menu.Item(...).Shortcut(...)` | `Widgets/Layers/MixtormatLayerMenus.cpp` etc. |
| Status bar | `WorkingStatusText` | Document/operation status; V/W/E feedback does not overwrite it. |
| Transient mode/action feedback | `SMixtormat_PreviewHints.cpp::BuildPreviewFeedback`, `ShowPreviewFeedback` | Modes, Q choices/successful edits and slider results; one active timer, no queued messages. Suppressed during active slider, navigation or owned drag-target guidance. |
| Interaction guidance | `SMixtormat_PreviewHints.cpp::GetInteractionHint` | Active slider → owned drag/navigation → open Q/control → selection hints (maximum four). Passive card hover and weak owners; `HelpDelay` settling, no widget-tree scan. |
| Slider context signals | `SMixtormatSlider`, `SMixtormat::MakeSlider` | Existing mouse/entry owners report hover, scrub and type-in. Snap guidance uses the live positive delta; successful value changes report once. |
| Viewport navigation signals | `SMixtormatPreviewViewport::GetNavigationHint`, client `InputAxis` | Actual MMB pan/LMB orbit/RMB lighting branches report the gesture; release/Escape and capture/focus loss clear it. Input handling is unchanged. |
| Stack drag guidance | `FMixtormatGuidedDragDropOp`, existing drop targets/handlers | Workspace-bound operation, weak target, current payload check. Shared preflights supply eligibility/rejection; leave/drop/cancel/expired targets clear guidance. ID Group reservation remains separate from source eligibility. |
| Empty states | Text blocks in panels | e.g. "No global variables yet.", "No layer selected". |
| Entry-commit rules | `UI/Controls/MixtormatEntryCommit.h` | Enter/Tab/click-away accept; Escape/right-click cancel — documented in the header. |

## Hotkey catalog — workspace (`SMixtormat`)

`Shift+1`–`Shift+5`, `L`, `P`, `G`, and the quick-controls `Q` close action use
the Slate input preprocessor (`SMixtormat.cpp::Construct`). The preprocessor
requires a focused widget within this specific `SMixtormat` workspace and
requires its top-level window to be active; it yields for editable text fields.
Shortcuts therefore do not act on Mixtormat when the user is focused in another
Unreal editor window, another tab, or another Mixtormat instance. Other
workspace shortcuts use `OnKeyDown` and follow normal Slate focus routing.
`OnKeyDown` must return `Handled()` only for matched Mixtormat actions; all
other keys (including Unreal's `Ctrl+Shift+S` Save All) remain unhandled so
normal editor commands work with Mixtormat focused or docked.

Workspace layout: Layers/Library/Global are the normal left-column rail/pages. Layers can
pop out and return by rail click or snap-back drag. The gallery is one MATERIALS/MASKS
bottom drawer over the whole workspace. `L` reads Home → Popped Out → Hidden; `G`
opens/closes the drawer. See the overlay-workspace handoff.

| Key | Action | Advertised where |
|---|---|---|
| `L` | Cycle the layer stack's placement: Docked → Overlay → Hidden → Docked. The left column's icon rail stays docked; only the stack travels | Top-bar tooltip `"Cycle Layers placement: Docked → Overlay → Hidden → Docked (L)."`; the control's label reads `Layers: Docked` / `Overlay` / `Hidden` |
| `P` | Cycle Inspector placement: Docked → Overlay → Hidden | Top-bar tooltip `"Cycle Inspector placement: Docked → Overlay → Hidden → Docked (P)."`; the control's own label reads `Inspector: Docked` / `Overlay` / `Hidden` |
| `G` | Collapse/expand bottom galleries | Gallery collapse tooltip `"… (G)."` |
| `R` / `I` | Toggle existing region-ID preview for the selected child; I retained as an alias | PostFX mode 3 reserved for future IDs; current IDs retain their existing rendering |
| `F2` / `F12` | Rename selected layer or group | Menus `.Shortcut("F2")`; F12 undocumented alias |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo (tooltip also names `Ctrl+Shift+Z`) | Top-bar tooltips |
| `Backspace` | Reset hovered numeric control | Slider tooltip (see below) |

## Hotkey catalog — viewport (`FMixtormatPreviewViewportClient::InputKey`)

Focus = the viewport. Clicking the preview's actual `SViewport` surface restores
keyboard focus immediately in `SMixtormatPreviewViewport::OnPreviewMouseButtonDown`,
even when returning from Inspector controls; no drag is required. Floating Inspector
and quick-control widgets are excluded from this focus handoff. Feedback varies by
action: camera/overlay changes, existing controls, the mode label and/or status bar.
Shortcut discoverability is incomplete.

| Key | Action |
|---|---|
| `F` | Frame the mesh (focus camera) — same meaning as elsewhere in the editor |
| `Space` or `H` | Toggle viewport overlay UI |
| `Z` (bare) | Toggle displacement — bare only, so `Ctrl+Z` still reaches undo |
| `V` | Cycle PBR material channels regardless of selection: Material → Base Color → Normal → Roughness → AO → Metallic → F0 → Height → Fuzz → Material. Not module diagnostics or PostFX output modes |
| `Shift+V` | Reset PostFX, module diagnostics and material-channel views; works in the viewport and workspace preview key handler |
| `W` | Activate selected eligible scalar/mask PostFX; same key on the same target toggles off, W↔E switches type, another eligible source transfers. An ineligible selection never dismisses an active overlay. Ownership survives selection and parameter edits. Viewport focus only; works during mouse drags |
| `R` (bare) | Toggle existing Region IDs preview through the workspace handler; viewport yields the key. PostFX mode 3 remains reserved, not implemented |
| `E` (bare) | Toggle persistent raw published Flow/UV PostFX (mode 1; Mask/scalar uses mode 0); same key on the same target toggles off, E↔W switches type, another supported source transfers the shared W/E owner. An ineligible selection never dismisses an active overlay. Viewport focus only; works during mouse drags; artist-authored appearance, built-in default when absent |
| `1`–`4` | Select Sphere / Plane / Cube / Cylinder; `2` flips an already selected Plane |
| `5` | Toggle complete material UV rotation by 90°, exactly like the preview marking-menu button |
| Mouse wheel | Zoom camera |
| LMB / MMB / RMB drag | Orbit camera / pan camera / rotate lighting; active owner guidance says release to finish |
| `Q` (bare) | Opens viewport quick controls: Render/Composition above, Lighting left, Geometry right, real context-sensitive Actions below. Q/Escape or mesh/light selection closes; cursor distance drives proximity fade/close except while tracked submenus are open. Opening is viewport-scoped; workspace close routing respects focus/text-entry guards. Not Tab. Source-checked; interaction testing remains owner-run. |

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

## Context-help upgrade proposal

See [context-help-q-plan.md](../auditdocs/workspace-layout/context-help-q-plan.md) for
source-checked Q capabilities and staged integration with corner hints/top-center feedback.
Increment 1 adds settled Q-card guidance, shared-slider hover/scrub/type-in guidance and
mesh/light/action/result feedback. The original Q focus gates, gestures, card geometry and
nested-menu behavior remain; proximity dismissal also pauses during slider interactions.
Increment 2 captures Q's layer/group/module GUIDs and names on opening. Actions shows the name;
its styled help/menu caption shows the full path without widening the card. Root and nested
callbacks resolve the captured identity and recheck availability instead of using later selection.
Document/history resets and Q close/reopen invalidate stale callbacks. Creation feedback names
the actual inserted child's owner; existing handlers still select their results normally.
Output Copy/Gates and unavailable nested rows remain visible with live styled disabled reasons.
`MixtormatMenu::FBuilder::UnavailableReason` owns the row gate and uses the existing tooltip
adapter, not local styling. Fill keeps insert-after-layer behavior; Material still appends.
Increment 3 adds owner-reported viewport manipulation and layer/group/module drag contexts.
Drag hover reuses extracted handler preflights and existing published-output/ID Group validators;
rejection text stays in the corner, not a per-move center announcement. Drop dispatch also checks
workspace/source identity and document revision before mutation; recognized rejected drops are
consumed rather than bubbling to another destination. Layer/group hover and commits share final
order/membership projections and published-output validation; empty-group membership commits
through the layer-move handler. Q Texture Mask now honors captured group-module scope through
the shared scoped-mask handler, while group-root masks remain group-wide. Active contexts replace
selection hints, which are capped at four with reset/selected-output priority. Short contexts
are not padded with unrelated actions. Q remains viewport-only with unchanged captured targets,
focus/capture, proximity and nested-menu rules. The two preview icon-button tooltips and group
accent swatches now use existing styled adapters. Owner checks are listed in the plan;
no commands, builds, tests or diagnostics were run for these increments.

## TODOs — architecture

- **Single source for key strings.** A key currently lives in three places
  (tooltip text, menu `.Shortcut`, handler). Consider one small shortcut
  table feeding tooltips and the menus; at minimum grep for the old text when
  a binding changes.
- **Workspace keys are split.** `L`/`P` work everywhere (input preprocessor);
  `G`/`I`/`F2` need workspace focus. Decide whether all workspace-level keys
  move to the preprocessor, or none. `Tab` is the counter-example: it must stay
  viewport-scoped, so the split is likely permanent.
- **Contextual help uses existing surfaces.** Q cards and shared sliders now report to the
  corner strip; viewport input owners and workspace-bound stack drags now do so too.
  Results reuse the top-center label. Specific explanations remain in `SMixtormatHelp`;
  do not add another tooltip/overlay system.
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
