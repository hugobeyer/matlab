# Context helpers + Q menu upgrade

2026-10-24 · Increments 1–3 implemented in source; owner compile/runtime checks pending.
Viewport/drag guidance and the three deferred styled-tooltip fixes are included.
Scope: existing Q menu, corner hint strip and top-center feedback. No new help system.

## Current code
- Q opens viewport quick controls at the pointer; bounds clamp it inside the viewport.
- Cards: Render + Composition above, Lighting left, Geometry right, Actions below.
- Actions supports layers, effects, IDs, generators, surfacing, masks, output copy and gates.
  It captures the layer/group/module identity when Q opens; later selection changes do not retarget it.
  Output Copy and Gates retain their row positions even when unavailable.
- Q/Escape dismiss; geometry/light selection closes; moving sufficiently far away fades/closes.
  Proximity dismissal is suspended while the tracked Actions/Final submenus are open.
- Opening remains viewport-scoped; workspace Q closes an open menu, with focus/text-entry guards.
- Corner hints prioritize active workspace sliders, owned drag/navigation contexts, open Q or
  settled control hover, then selection/preview state. Selection hints are capped at four.
  Q shows Q/Escape close plus settled card guidance. A slider scrub/type-in takes priority;
  Escape is advertised as cancel while typing, not as menu dismissal.
- Top-center feedback reports V/W/E/reset, Q mesh/light/reset choices, successful Q additions
  and output copies, plus changed slider values on release/capture loss/accepted type-in.
  It reuses the 1.4s hold + 0.5s fade and stays hidden during active slider, viewport-navigation
  or owned drag-target guidance. Drag hover does not emit per-move center feedback.
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

## Increment 2 — implemented
- One immutable Q snapshot captures owner/child GUIDs, display names and document revision.
  Actions shows the captured name; its styled help and menu caption show the full target path.
  The existing Actions width metric constrains the title, so long names cannot move the cards.
- Root and nested callbacks retain the snapshot. Shared creation callbacks resolve current layer
  indices from the owner GUID immediately before invoking the existing handler; scoped child
  identities survive reordering. No temporary selection changes are used to route actions.
- Q close/reopen invalidates old callbacks. A history-baseline/document reset invalidates the
  document revision, including reopening the same asset. Missing or moved-to-another-owner
  targets are rejected rather than falling back to selection. Creating the first material also
  changes the revision: reopen Q before further actions in that new document.
- Shared creation menus use live `CanCreateChild`, generator/surfacing/Flow checks; output copy
  and gate callbacks reuse their existing copy/paste/scope validators. Rejections reuse the
  center label. Creation feedback checks the actual destination, not `FReply::Handled()`.
- Output Copy/Gates and unavailable nested rows stay visible. `FBuilder::UnavailableReason`
  disables a row and feeds its reason into the existing `SMixtormatHelp` tooltip adapter.
  Reasons do not change row text, width or order. No new help surface or styling was added.
- Target semantics remain explicit: effects/generators go to the captured layer root; IDs and
  surfacing use the captured layer/group root. Masks use the supported captured module or its
  root; gates/output copy use the captured module. Supported group-root creation now reuses the
  existing group handlers; unsupported generator/effect entries remain disabled with reasons.
- Fill inserts after the captured layer (group/no selection appends). Material keeps its existing
  append behavior. Library source selections remain inputs to those existing handlers; they are
  not captured owner identities. Texture-mask submenus retain their existing asset-path capture.
- Inferred creation scope reads the captured child GUID for Q, not a later selected-child index.
  Ordinary context-menu signatures and handlers remain in place.
- Q opening remains viewport-only. No input preprocessor, focus, capture, close gesture, preview
  shader or Moss implementation changes. No commands/builds/tests/diagnostics were run.

## Increment 3 — implemented
- Viewport InputAxis reports only gestures it actually handles: MMB pan, LMB orbit, RMB lighting.
  The corner describes the active drag and release-to-finish; Escape is not advertised as undo.
  Changed-owner notifications invalidate hint layout. Release/Escape clears the report; the
  viewport lifecycle and live capture/focus/button checks suppress stale navigation guidance.
  Camera/light handlers, mouse replies, focus/capture and Q opening/closing routes are unchanged.
- Stack layer/group/module operations own their guidance. Only the originating workspace reads
  it; targets report their existing before/after/into routes. Weak target/owner lifetimes, live
  source identity/document-revision checks, leave and drop/cancel lifecycle prevent retained
  target guidance.
- Hover and mutations share extracted reorder/move/reparent preflights, including existing
  published-output validation/rejection text. Invalid placements without a specific existing
  reason show a blocked-placement explanation rather than promising a successful drop.
  ID Group rows retain their routing reservation while eligibility uses CanAddIdGroupSource;
  guidance explains that linking Region IDs leaves the source module in place.
- Existing zones, decorators and mutation handlers remain. Recognized stack-drop rejections
  are now consumed instead of bubbling to another destination (see corrections below).
  A handled drop is not interpreted as success. Group-edge snapping and no-op placements are
  described explicitly. No library-drag acceptance rules or new interaction gestures were added.
- Active contexts replace selection hints. Selection hints keep at most four eligible items,
  prioritizing diagnostic reset and selected outputs over general preview shortcuts; row order
  and shortcut handlers remain unchanged. Short contexts are not padded with irrelevant actions.
- Existing corner/center surfaces retain styling and H/Space visibility. Q's slider-specific
  proximity suspension, captured targets and nested-menu lifecycle are unchanged.
- Both preview icon-button tooltip paths now use their existing styled ToolTip(FText) argument;
  group-accent swatches use MakeStyledToolTip without changing their appearance or handlers.
- Source edits only: no commands, builds, tests, diagnostics or Unreal launches were run.
  Rust, Moss and GPU implementation work is untouched.

## Follow-up corrections — five reported source findings
- Stack drop dispatch now checks the receiving handler's workspace, source GUID/index identity
  and document revision immediately before invoking it. Guidance checks alone were insufficient.
  Foreign/stale drops are consumed without edits; ID Group non-reserved rows still defer to
  their enclosing reorder/move target.
- Layer/group moves share projected-order helpers between hover checks and commits. The existing
  PublishedOutputPlacementsValid validator checks final order and group membership before working
  arrays are replaced. Rejections retain the document and report a published-output placement
  reason through existing feedback/status paths; height-reference remapping behavior is retained.
- Dropping a layer into an empty group now commits both projected layers and groups through the
  existing layer-move handler, including membership, history, selection and preview refresh.
- A recognized child-row drop consumes both validation failure and a blocked zone, preventing a
  rejected before-module placement from bubbling into an unintended layer-body append. Shared
  published-output preflights now supply an explicit failure reason rather than an empty one.
- Q Texture Mask honors a captured group module's ScopeOwnerChildId via the shared address-based
  scoped-mask handler. Group-root creation is unchanged; nested Q capture/revalidation remains.
- These are source fixes, not claims about when the faults were introduced or runtime validation.
  No commands, builds, tests or diagnostics were run.

## Priorities
1. **Q guidance + feedback.** While open, replace generic V/W/E hints with Q/Escape close
   and the hovered card/control's real interaction hints. Show result feedback after mesh,
   lighting and action choices. Keep slider guidance during a scrub; summarize on release,
   rather than emitting a new message per mouse move. Preserve nested-menu/text-entry behavior.
2. **Explicit action target.** Show the selected layer/group/module name in Actions.
   Capture its stable identity when Q opens; revalidate before execution. Root and nested
   callbacks now retain the snapshot, including shared creation and output-copy builders.
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
- `Widgets/Layers/MixtormatQuickControlsActions.cpp`: Q target snapshot, resolver and availability.
- `Widgets/SMixtormat.cpp::ResetEditHistory`: document-revision invalidation.
- `UI/Menus/MixtormatMenuBuilder.*`, `SMixtormatMenuItem.*`: live disabled reasons and styled help.
- `Widgets/SMixtormatPreviewViewport.*`: actual navigation owner and capture/focus lifecycle.
- `UI/DragDrop/MixtormatDragDropOps.h`, `SMixtormatDropTargets.h`: operation-owned target guidance.
- `Widgets/Layers/MixtormatLayerDragDrop.cpp`, `MixtormatLayerHierarchy.cpp`,
  `MixtormatLayersPrivate.h`: shared drop preflights, source binding and ID Group routing.

## Remaining owner checks
- Build/compile, then check viewport edges and multiple DPI scales; cards must not move on hover.
- Repeated Q, Escape, mesh/light close and proximity dismissal; nested Actions/Final menus.
- Slider click-to-type, Enter/Tab/click-away accept, Escape/right-click cancel; no focus theft.
- Shift/Ctrl+Shift/Ctrl scrub modifiers, zero snap delta, cursor wrapping and capture loss.
- One result on release, none per move or cancelled/unchanged entry; repeated actions replace it.
- Q additions/copies name the result and actual scope; no success label on failed insertion.
- Inspector/shared-slider guidance clears on leave, rebuild, disabled controls and theme changes.
- H/Space hides both optional surfaces; material-contract warnings remain separate.
- Q on layer/group/module/no selection: target title/path and root-versus-scoped destinations.
- Change selection with a nested menu open; reorder layers/modules before choosing an action.
  The original identity must receive the edit; deleted/cross-owner-moved targets must reject it.
- Close/reopen Q or reset/open a document (including the same asset); old callbacks must not act.
- Test Fill/Material, every creation submenu, output copies, Noise/Gallery/Paste Gates and filters.
- Change clipboard, output publication, instance state and scope capacity with menus open;
  disabled reasons must update without moving rows or allowing stale nested actions.
- Long target names, group targets and first-material creation; no card movement or focus theft.
- Ordinary context menus still use their existing handlers and selection-routing behavior.
- LMB orbit/MMB pan/RMB lighting, mixed held buttons, release/Escape, focus and capture loss;
  only the active viewport should supply navigation guidance, with no camera/input changes.
- Layer/group before/after/into, group-edge snapping/no-op, module reorder/reparent/unshare/share;
  published-output rejection, scoped filters, invalid nesting and ID Group source eligibility.
- Drag leave, Escape cancellation, drop, source edits, document/theme rebuild and multiple
  workspaces: no stale or cross-workspace guidance or mutation. Recognized rejected drops must
  not bubble into an append or another destination.
- At most four selection hints; active drag/navigation/slider guidance replaces them and hides
  center feedback. Check long rejection text, small windows, custom themes and H/Space.
- Move producer/consumer layers and groups across each other, including membership changes;
  rejected moves must leave arrays, selection and undo history unchanged. Check valid moves too.
- Drop into empty groups: membership persists through selection, preview refresh and undo/redo.
- Q Texture Mask on a group module versus group root: verify mask scope, selected result and
  feedback; change selection/reorder/delete the captured module while the texture submenu is open.
- Preview feature/output eyes and group-accent swatches: styled hover timing/placement without
  changed click, focus, capture or menu behavior.
