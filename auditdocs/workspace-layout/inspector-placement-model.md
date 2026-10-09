# Inspector Placement Model

Extends `ui-layout-and-panels-audit.md` §6. The Inspector remains a docked right
column or viewport overlay under the new D30–D32 plan; old references below to a
resizable Layers column / left shell splitter describe pre-D30 source only. See
`AgentDocs/UI.md` for the current pinned-rail and shared-left-overlay model; the original
handoff is archived in `AgentDocs/old_docs/overlay-workspace-handoff.md`.

Status: placement-cycle prototype implemented
(2026-10) — `P` and the top-bar placement control cycle Docked → Overlay → Hidden.
One inspector is reparented between dock and viewport hosts; Hidden stays attached
in the collapsed dock host for theme layout-state transfer. Overlay uses a square,
borderless shell-colored surface, floats at the viewport's right edge, and is dragged
by its header row and resized from all four corners, clamped on entry and during
drag/resize (a re-clamp on viewport resize is still open — O12).
Corner targets show a small L outline only on hover; the opposite corner stays fixed. The
Layers column stays resizable in every placement: while the inspector column is away
its share is carried by the centre slot, so the splitter still divides the full width.
Source wiring checked. Later user builds reported compile errors in unrelated recent widget edits; those
were corrected locally, but a successful follow-up build has not been reported. UI
behaviour has not been tested in the editor. Persistence, selection-driven
Auto visibility and collapse-to-top remain deferred; D20 auto-fit height is separate.

## Goal

The inspector should stop being a permanently docked right column and become a
placement-aware panel: an overlay in the viewport, dockable right, hidden, or
auto-shown on selection — with a draggable popover mode that collapses
vertically to a top bar.

## Current state (verified)

- Right column of the shell splitter; `BuildInspectorPanel()` returns
  `SBox.WidthOverride(MixtormatTokens::InspectorWidth)` (`MixtormatDesignTokens.h`
  L626; a lambda now, so Overlay returns `FOptionalSize()`) containing:
  - identity row (name + badge) — `SMixtormat_Inspector.cpp` L459–504; it doubles
    as the overlay's drag handle
  - instance banner — `BuildInstanceBanner()`, L505–508
  - ~~GLOBAL placeholder group~~ — moved to the left column as the third tab
    (`BuildGlobalPage`); the inspector no longer hosts it (D13).
  - two mutually exclusive scroll boxes: child inspector (gated by
    `HasSelectedChildInspector()`, L511–561) and the layer inspector (the
    inverse, L562+).
- No separate header bar: the existing identity row is used as the drag handle.
- Selection state: `HasAnySelection()` (`SMixtormat.h` L664), `bHasSelectedLayer`,
  `SelectedLayerIndex`, `SelectedGroupId`, `SelectedGroupChildIndex`.
- Undo clears multi-selection, group selection and selected effect/mask indices,
  but retains/clamps `SelectedLayerIndex` and derives `bHasSelectedLayer` from it
  (`SMixtormat.cpp` L299–311). `HasAnySelection()` tests that layer index or the
  group ID (`Widgets/Layers/MixtormatLayerHierarchy.cpp` L778–781), so undo does
  **not** necessarily leave the inspector without selection.

## Placement modes

| Mode | Behavior |
|---|---|
| Docked Right | Current behavior; right splitter slot |
| Overlay | Same inspector; defaults to the right edge, then keeps the user's dragged position |
| Hidden | Collapsed away entirely |
| Auto (deferred) | Overlay shown on selection, hidden otherwise (see state model) |

The cycle (`P` and the placement control): **Docked → Overlay → Hidden →
Docked** (D15). The overlay keeps the inspector's layout with **no outer
border and no rounded corners** (D16); inner component styling is unchanged.

Implementation follows audit §6 option B: one live inspector per workspace;
`BuildAuthoringPage()` builds its replacement on reconstruction and placement
changes reparent it without rebuilding (`SMixtormat_Shell.cpp` L362–367, L705–709).
Hidden keeps it in the collapsed dock host. The
drag/resize state and handlers are inspector-specific on `SMixtormat` today
(`SMixtormat.h` L1494–1514, `SMixtormat_Shell.cpp` L685–877). Extracting a generic
overlay controller is the prerequisite for D22 — do not fork a second drag/resize
implementation.

## Overlay geometry: width, height and inset

Implemented so far: explicit width and height, four corner resize targets, viewport
clamping (entry/drag only — O12), and a default placement flush to the viewport's
right edge at full height.
The following changes that model.

| Axis | Behaviour |
|---|---|
| Width | Explicit and resizable, as today |
| Height | **Auto-fit by default**: the panel is as tall as its content, capped at the viewport height. A drag on a top or bottom corner switches it to an explicit height |
| Inset | On first entry the overlay is placed **inset from the viewport edges by a token**, not flush |

Why auto-fit: the inspector's content is a stack of foldouts. Collapsing them today
leaves the panel at its old height with dead space below the last card, which reads as
a broken panel rather than a collapsed one. With auto-fit, collapsing a foldout shrinks
the panel to the content, and the panel grows again when it is expanded.

Rules:

- Auto-fit is capped at the viewport height; past that the inspector's own scroll box
  takes over, exactly as it does in the docked column.
- An explicit height is the user's choice: foldout collapse then leaves the panel size
  alone and the scroll box absorbs the change. Do not silently snap back to auto-fit.
- Auto-fit must not fight the drag. Measure the active scroll content (including
  foldouts) plus identity row/banner, at the panel's current width. Do not measure
  the explicit-height host or rely solely on the scroll viewport's desired height.
  The current inspector uses two mutually exclusive `FillHeight` scroll boxes
  (`SMixtormat_Inspector.cpp` L510–578); preserve finite scroll allocation when the
  content exceeds the available height. Verify foldout/selection/width changes and
  avoid a desired-size/height-override feedback loop.
- The inset is a token (`InspectorOverlayInset` or similar), not a literal, and applies to
  the default placement only — a dragged panel keeps wherever the user put it.
  Its own token: do not alias `PreviewLayout.OverlayInset` / `ViewportOverlayInset`,
  which space the viewport toolbars.
- Height needs a retained Auto/Explicit state, separate from selection-driven Auto
  visibility. All four existing corners affect both axes (`SMixtormat_Shell.cpp`
  L784–801); a resize gesture makes height explicit. Freeze auto-fit during drag
  and resize; restore measurement only if the height remains Auto.
- Re-clamp on viewport resize is still missing: `ClampInspectorOverlay` runs on
  entry and during drag/resize only (O12).
- D23 resolves O10: add a small **Fit height** action to return Explicit height to
  Auto. Keep width/position unchanged, except for necessary viewport clamping.
  Reopening or cycling placement retains the height mode and remembered explicit
  height; neither resets to Auto. Foldout collapse/expansion fits content only in
  Auto mode and leaves Explicit height alone. Fit height is planned, not implemented.
  This does not add the separately deferred collapse-to-header feature.

## Layers placement — the same model

The left column gets the same treatment as the inspector: **Docked → Overlay → Hidden**,
with one instance reparented between hosts, never a second copy.

| Piece | Approach |
|---|---|
| Scope | Decided (D24): the whole LAYERS / LIBRARY / GLOBAL panel and tab strip travel together, one instance |
| Hotkey | Decided (D24): `L` cycles Docked → Overlay → Hidden → Docked. Update the top-bar label, tooltip and HELPERS catalog with implementation; source still collapse/expands today |
| Splitter | The left slot collapses while floating; the centre slot carries its share and hands it back on write-back — the same pattern the inspector column already uses |
| Styling | Square, borderless, translucent — the same tokens as the inspector overlay |
| Default placement | Inset from the viewport's left edge, auto-fit height |
| Reuse | The existing `bLeftPanelCollapsed` machinery and the inspector's overlay host/drag/resize code are the base; do not fork a second implementation |

Consequences to plan for:

- The left slot has only `bLeftPanelCollapsed` today (`SMixtormat_Shell.cpp`
  L387–403). Its share is already lent to the centre, but centre/right write-back
  is suppressed while the left panel is collapsed (L405–424, L518–528). Add an
  independent left placement state and symmetric borrowed-share write-back.
  A floating Layers panel resizes through grips, not a hidden shell handle; the
  remaining docked Inspector must still resize. Test all nine left/right placement
  combinations, normalized fractions and restoration after reconstruction.
- Two overlays can be open at once. Keep independent geometry and gesture state;
  route each press to the topmost hit panel and bring the clicked panel to front
  (D25). Do not front a panel behind the hit target. Specify
  Layers' minimum size before reusing inspector-only limits
  (`MixtormatDesignTokens.h` L624–634); share tokens only where semantics match.
- Cancel capture/gesture state before reparenting or reconstruction, and on capture
  loss. The current handlers capture `SMixtormat` and retain interaction flags
  (Shell L766–855), so retained state alone does not make a mid-drag rebuild safe.
- Viewport controls stay anchored to the full viewport (see
  `AgentDocs/old_docs/viewport-quick-controls-plan.md` (archived)), so a floating Layers panel may cover the left rail.
  That is accepted: controls do not move to avoid overlays.
- Out of scope: dragging a panel between columns, docking to the opposite side,
  persistence and gallery-popover/auto-collapse changes. D25 keeps the existing
  bottom-docked gallery toggle unchanged.
- Test each stage after implementation (D29). The user will exercise placement,
  four corners, Fit height, nine splitter combinations and theme reconstruction;
  build success alone does not prove these behaviours.

## Historical Auto-visibility proposal — deferred and not implementation instructions

| State | Overlay |
|---|---|
| Selection exists | Shown — selection content |
| No selection + manual open | Empty-selection inspector; GLOBAL stays in the left panel (D13) |
| No selection + not manually opened | Hidden |
| Pin active | Always shown; auto-hide suppressed |

- This table is a future proposal. `P` currently cycles placement (D15), not
  manual Auto visibility; do not replace that behaviour without a new decision.
  No Pin control is implemented.
- Undo can change selection, but retains a valid layer index when layers remain
  (`SMixtormat.cpp` L299–311). Do not assume the overlay must hide on every undo.
- Empty-space click deselects → hides (desired).
- Build once, toggle visibility via lambda — never rebuild on selection.

## The GLOBAL section — superseded

The inspector's pinned placeholder was moved out: document-scope content is
now the GLOBAL third left-column tab (D13, `BuildGlobalPage`). The variables
cell lives there, not in the inspector; the inspector stays selection-only.

## Showing variables and selection content at the same time

The driving workflow is: scrub a variable, watch a driven parameter change. That
requires variables and selection content to coexist.

| Where | Both visible? | Cost |
|---|---|---|
| Pinned group in inspector (superseded — D13) | Yes — variables on top, selection scrolls below | Was smallest; the user chose the GLOBAL tab instead |
| Bottom drawer third tab (Materials / Masks / Variables) | Yes — full width for sliders | Small; drawer eats viewport height, often collapsed |
| Left panel third tab (chosen — D13) | Yes, but loses the layer stack while editing variables | Small; the user accepted the tab trade-off |
| Separate overlay / floating window | Yes | Most chrome; fights the "max viewport" goal |

With the GLOBAL tab, the driving layout is: left column on GLOBAL (variables),
viewport center, inspector right — selection content and variable sliders
coexist because they sit in different columns.

The old variables-group auto-hide condition is superseded by D13: the variables
are not in the inspector. Keep manual placement unchanged in this implementation;
a future selection-driven Auto mode needs its own decision.

## Draggable popover with vertical collapse to top

Assessment: **medium, not painful.**

| Piece | Size | Notes |
|---|---|---|
| Overlay placement | Small | Audit §6 option B |
| Collapse to top | Small | Height override → header row; the identity row (L459–504) already works as the collapsed bar; instant, matching the gallery collapse |
| Draggable position | Medium | The only genuinely new machinery; no in-repo precedent before the cycle landed (now implemented, inspector-specific — see I6) |
| Resize | Implemented baseline | Four corner grips, mouse capture + delta; keep these, do not replace with one edge handle |
| Persistence | Small | Position, size, collapsed → `UMixtormatEditorSettings` (additive) |

### The drag is the real work

- Standard tool: `SConstraintCanvas` (anchors + offsets) inside the existing
  `SOverlay` slot in `BuildPreviewPanel`. The shipped overlay positions itself by
  slot padding with a lambda instead (`SMixtormat_Preview.cpp` L1637–1653);
  either is fine.
- Drag handle = the header row; mouse capture + delta.
- Clamp to viewport bounds, and re-clamp on window resize. **The re-clamp is still
  missing** (O12); clamping only runs on entry and during the drag.
- Position/size/collapsed state on `SMixtormat` so it survives theme
  reconstruction.

### Risks

- Retain placement, position, size and height mode on the workspace; keep Hidden
  content attached for `TransferLayoutState`. Its scroll/group transfer is by tree
  order, not stable IDs (`SMixtormat_Theme.cpp` L22–76): keep capture/restore topology
  identical, including when either panel is hidden or floated.
- Structural metrics use tokens; live-retunable values need the theme model,
  schema/defaults and a suitable refresh mode. No local styling or dependencies.
- Hit-testing is untested. Keep full-viewport positioning containers self-hit-test
  invisible with interactive panel children; empty space must reach the viewport.
- `SMixtormatPopupAnchor` intersects its boundary with the SDockTab rect
  (L108–115). That does not prove every menu uses it, nor clamp to the preview:
  verify menus and capture after reparenting.

### Bonus

The collapsed bar doubles as the Auto mode's minimal state: ~30px pinned at the
top, selection name always visible, expand to edit. That solves the show-both
problem cleanly: bar + viewport + layer stack, all visible.

## Effort summary

| Scope | Size |
|---|---|
| Overlay + collapse-to-top + toggle + hotkey | Small–medium |
| Draggable + clamp + persist | Medium |
| Resize handle | Small–medium |
| Variables pinned group | Superseded by D13; not part of this implementation |
