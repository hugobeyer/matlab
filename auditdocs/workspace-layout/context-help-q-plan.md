# Context helpers + Q menu upgrade

2026-10-24 · Increment 1 implemented in source; owner runtime checks pending.
Target snapshotting and the remaining interaction contexts are still planned.
Scope: existing Q menu, corner hint strip and top-center feedback. No new help system.

## Current code
- Q opens viewport quick controls at the pointer; bounds clamp it inside the viewport.
- Cards: Render + Composition above, Lighting left, Geometry right, Actions below.
- Actions already supports layers, effects, IDs, generators, surfacing, masks, output copy
  and gates. Some entries depend on the selected child; it is not a placeholder.
- Q/Escape dismiss; geometry/light selection closes; moving sufficiently far away fades/closes.
  Proximity dismissal is suspended while the tracked Actions/Final submenus are open.
- Opening remains viewport-scoped; workspace Q closes an open menu, with focus/text-entry guards.
- Corner hints prioritize active workspace sliders, open Q, then selection/preview state.
  Q shows Q/Escape close plus settled card guidance. A slider scrub/type-in takes priority;
  Escape is advertised as cancel while typing, not as menu dismissal.
- Top-center feedback reports V/W/E/reset, Q mesh/light/reset choices, successful Q additions
  and output copies, plus changed slider values on release/capture loss/accepted type-in.
  It reuses the 1.4s hold + 0.5s fade and stays hidden during an active slider interaction.
- No directional flick-to-execute or hold-Q/release gesture was found in inspected Q handlers.

## Shared roles
| Surface | Purpose |
|---|---|
| Corner hints | What can I do here? At most 2–4 relevant actions/modifiers. |
| Q menu | Perform existing actions with stable card positions and an explicit target. |
| Top-center label | What happened? Confirm a mode/action or explain rejection. |
| Styled hover help | Explain the specific control; do not duplicate its paragraph in the corner. |

## Increment 1 — implemented
- Passive Q-card hover signals reuse `HelpDelay`; no widget-tree scanning or new tooltip surface.
- Shared `SMixtormatSlider` signals feed Q and Inspector guidance through `MakeSlider`.
  Hover: adjust/type/reset. Scrub: fine/finer and snap only with a positive snap delta.
  Type-in: accept/cancel. No feedback per mouse move; unchanged/cancelled entries stay silent.
- Q proximity dismissal pauses during a slider interaction, including typing and cursor wrapping.
  Existing Q/Escape routing, mouse replies, capture, focus and menu/card layout are unchanged.
- Mesh/lighting choices and camera/lighting reset reuse their current handlers.
- Creation handlers report only after insertion; shared child feedback names the actual scope
  owner or layer/group. Fill/material actions and output copy also report their actual result.
  Material creation reports rejection when no usable library surface is selected.
- Corner guidance and center feedback use the existing typography/plates and H/Space flag.
  Active slider guidance suppresses selection shortcuts and hides center feedback until it ends.
- No commands, builds, tests, diagnostics or Unreal launches were run. Static source review only.

## Next increments — not implemented
1. Capture stable action identity on Q open, show the destination, and revalidate each action.
   Include shared submenu callbacks, not just `BuildQuickControlsActionsMenu`: they capture
   layer indices, and `CreateChild` can infer scope from the current selected child.
   Preserve existing routing/validators; do not temporarily change selection to execute actions.
2. Keep unavailable Output Copy/Gates entries in place with authoritative disabled reasons;
   add rejection feedback where handlers currently return `Handled()` without changing data.
3. Extend owner-reported contexts to viewport orbit/pan/light rotation and validated layer/module
   drop destinations. Slider hover/scrub/type-in is already wired through the shared helper.
4. Bound the remaining selection-hint combinations to 2–4 items without changing hotkeys.

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
- `UI/Controls/SMixtormatSlider.h/.cpp`: interaction/feedback delegates; input handling is unchanged.
- `Widgets/SMixtormat.cpp::MakeSlider`: workspace binding for Q/Inspector and other shared sliders.
- `Widgets/Layers/MixtormatLayerActions.cpp`: successful creation feedback with actual owner names.
- `Widgets/Layers/MixtormatLayerClipboard.cpp`: output-copy and pasted-gate feedback.

## Remaining owner checks
- Build/compile, then check viewport edges and multiple DPI scales; cards must not move on hover.
- Repeated Q, Escape, mesh/light close and proximity dismissal; nested Actions/Final menus.
- Slider click-to-type, Enter/Tab/click-away accept, Escape/right-click cancel; no focus theft.
- Shift/Ctrl+Shift/Ctrl scrub modifiers, zero snap delta, cursor wrapping and capture loss.
- One result on release, none per move or cancelled/unchanged entry; repeated actions replace it.
- Q additions/copies name the result and actual scope; no success label on failed insertion.
- Inspector/shared-slider guidance clears on leave, rebuild, disabled controls and theme changes.
- H/Space hides both optional surfaces; material-contract warnings remain separate.
- After increment 2: selection/reorder/delete/document invalidation and stable disabled reasons.
- After increment 3: viewport capture and validator-backed drag acceptance/rejection.
