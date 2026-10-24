# Context helpers + Q menu upgrade

2026-10-24 · Source-checked proposal, not implemented or runtime-tested.
Scope: existing Q menu, corner hint strip and top-center feedback. No new help system.

## Current code
- Q opens viewport quick controls at the pointer; bounds clamp it inside the viewport.
- Cards: Render + Composition above, Lighting left, Geometry right, Actions below.
- Actions already supports layers, effects, IDs, generators, surfacing, masks, output copy
  and gates. Some entries depend on the selected child; it is not a placeholder.
- Q/Escape dismiss; geometry/light selection closes; moving sufficiently far away fades/closes.
  Proximity dismissal is suspended while the tracked Actions/Final submenus are open.
- Opening remains viewport-scoped; workspace Q closes an open menu, with focus/text-entry guards.
- Corner hints mainly inspect selection/preview state. Q currently reduces them to “Esc: Close”.
- Top-center feedback now reports V/W/E/reset results with a 1.4s hold + 0.5s fade.
- No directional flick-to-execute or hold-Q/release gesture was found in inspected Q handlers.

## Shared roles
| Surface | Purpose |
|---|---|
| Corner hints | What can I do here? At most 2–4 relevant actions/modifiers. |
| Q menu | Perform existing actions with stable card positions and an explicit target. |
| Top-center label | What happened? Confirm a mode/action or explain rejection. |
| Styled hover help | Explain the specific control; do not duplicate its paragraph in the corner. |

## Priorities
1. **Q guidance + feedback.** While open, replace generic V/W/E hints with Q/Escape close
   and the hovered card/control's real interaction hints. Show result feedback after mesh,
   lighting and action choices. Keep slider guidance during a scrub; summarize on release,
   rather than emitting a new message per mouse move. Preserve nested-menu/text-entry behavior.
2. **Explicit action target.** Show the selected layer/group/module name in Actions.
   Capture its stable identity when Q opens; revalidate before execution. Current callbacks
   often re-read selection when a submenu opens/clicks, so snapshotting is new work.
   Disable unavailable actions with a short reason; retain entry positions and existing actions.
3. **Broader interaction contexts.** Reuse the same hint selection for viewport orbit/pan/light
   rotation, inspector slider hover/scrub/type-in, and layer/module drag destinations.
   Use actual capabilities/validators for outputs, masks and drop eligibility—not guessed rules.

## Useful examples — proposed
- Hover Lighting in Q: explain preset selection; after selection: “Lighting · <preset>”.
- Scrub a Q/Inspector slider: Shift fine, Ctrl+Shift finer, Ctrl snap only when snap is supported.
- Hover an eligible module: W/E/R output guidance only when the action is available there.
- Open Actions: name the captured destination; after adding a gate, name the actual owner.
- Drag a layer/module: describe a valid destination or its rejection; do not promise unimplemented gestures.

## Keep it predictable
- Priority: active operation → open menu/control → focused/hovered area → selected module → general hints.
- Explicit signals from input owners; no per-frame whole-widget-tree scanning or AI service.
- Small context resolver shared by both helper surfaces; action handlers remain authoritative.
- Brief hover settling delay; no text flicker, message queue or per-tick slider announcements.
- Corner stays while relevant; center confirms then fades. Avoid duplicate/overlapping messages.
- Reuse PreviewLabel, ContextLayout, preview plates, SMixtormatHelp and existing Q theme metrics.
- Keep H/Space visibility behavior; persistent material-contract warnings stay separate.
- No Q expansion into layer/inspector focus, hold/release gestures or reordered cards without approval.
- Preserve clicks, keyboard focus, mouse capture and close behavior. Moss/GPU internals stay untouched.

## Source map / owner checks
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_PreviewControls.cpp`: Q layout/open/close.
- `Widgets/Layers/MixtormatLayerMenus.cpp::BuildQuickControlsActionsMenu`: available actions/targets.
- `Widgets/SMixtormat_PreviewHints.cpp`: corner hints + transient feedback.
- `Widgets/SMixtormat.cpp`, `SMixtormatPreviewViewport.cpp`: focus gates and actual input handlers.
- `UI/Controls/SMixtormatSlider.cpp`: editing, capture and modifier behavior.
  Short paths above are under `Source/MixtormatEditor/Private/`.
- Owner checks after implementation: viewport edges/DPI, nested menus, text entry, capture loss,
  repeated Q, context invalidation, disabled reasons, H/Space and theme reconstruction.
