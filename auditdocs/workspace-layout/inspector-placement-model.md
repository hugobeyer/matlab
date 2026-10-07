# Inspector Placement Model

Extends `ui-layout-and-panels-audit.md` §6. Status: placement-cycle prototype implemented
(2026-10) — `P` and the top-bar placement control cycle Docked → Overlay → Hidden.
One inspector is reparented between dock and viewport hosts; Hidden stays attached
in the collapsed dock host for theme layout-state transfer. Overlay uses a square,
borderless shell-colored surface, floats at the viewport's right edge, and is dragged
by its header row and resized from all four corners, clamped to the viewport.
Corner targets show a small L outline only on hover; the opposite corner stays fixed. The
Layers column stays resizable in every placement: while the inspector column is away
its share is carried by the centre slot, so the splitter still divides the full width.
Source wiring checked; Unreal build/UI validation pending. Persistence, Auto mode and
collapse-to-top remain deferred.

## Goal

The inspector should stop being a permanently docked right column and become a
placement-aware panel: an overlay in the viewport, dockable right, hidden, or
auto-shown on selection — with a draggable popover mode that collapses
vertically to a top bar.

## Current state (verified)

- Right column of the shell splitter; `BuildInspectorPanel()` returns
  `SBox.WidthOverride(MixtormatTokens::InspectorWidth)` (`MixtormatDesignTokens.h`
  L626) containing:
  - identity row (name + badge) — `SMixtormat_Inspector.cpp` L396–405
  - instance banner — L408–411
  - ~~GLOBAL placeholder group~~ — moved to the left column as the third tab
    (`BuildGlobalPage`); the inspector no longer hosts it (D13).
  - two mutually exclusive scroll boxes: child inspector (gated by
    `HasSelectedChildInspector()`, L497–500) and layer inspector (the inverse),
    with generator layers getting their own group (L509–513) and regular layers
    the standard sections (L529–533).
- No header bar exists. No free-positioned panel exists anywhere in the
  codebase.
- Selection state: `HasAnySelection()` (`SMixtormat.h` L657), `bHasSelectedLayer`,
  `SelectedLayerIndex`, `SelectedGroupId`, `SelectedGroupChildIndex`.
- Undo clears selection: `ApplyEditHistoryState` resets `SelectedLayerIds`
  (L245).

## Placement modes

| Mode | Behavior |
|---|---|
| Docked Right | Current behavior; right splitter slot |
| Overlay | Same inspector over the viewport's right edge |
| Hidden | Collapsed away entirely |
| Auto (deferred) | Overlay shown on selection, hidden otherwise (see state model) |

The cycle (`P` and the placement control): **Docked → Overlay → Hidden →
Docked** (D15). The overlay keeps the inspector's layout with **no outer
border and no rounded corners** (D16); inner component styling is unchanged.

Implementation follows audit §6 option B: one instance, placement decided in
`BuildWorkspaceUI`; never two instances (duplicate scroll/expansion state).

## Overlay geometry: width, height and inset

Implemented so far: explicit width and height, four corner resize targets, viewport
clamping, and a default placement flush to the viewport's right edge at full height.
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
- Auto-fit must not fight the drag. Resolve the height once per layout from the content's
  desired size, and only while the height is auto.
- The inset is a token (`InspectorOverlayInset` or similar), not a literal, and applies to
  the default placement only — a dragged panel keeps wherever the user put it.
- Open: whether a control returns an explicitly-sized panel to auto-fit. Out of scope for
  the prototype; cycling the placement is the current escape hatch.

## Layers placement — the same model

The left column gets the same treatment as the inspector: **Docked → Overlay → Hidden**,
with one instance reparented between hosts, never a second copy.

| Piece | Approach |
|---|---|
| Scope | The left panel as a whole — the LAYERS / LIBRARY / GLOBAL tab strip travels with it. Open: whether only the LAYERS tab should float |
| Hotkey | `L` becomes the cycle, matching `P`. The top-bar control and its tooltip change with it |
| Splitter | The left slot collapses while floating; the centre slot carries its share and hands it back on write-back — the same pattern the inspector column already uses |
| Styling | Square, borderless, translucent — the same tokens as the inspector overlay |
| Default placement | Inset from the viewport's left edge, auto-fit height |
| Reuse | The existing `bLeftPanelCollapsed` machinery and the inspector's overlay host/drag/resize code are the base; do not fork a second implementation |

Consequences to plan for:

- The left slot's write-back guard currently suppresses while collapsed. It must become
  placement-aware the way the inspector column's did, or the Layers column stops being
  resizable while floating.
- Two overlays can be open at once. They must not share drag state, and each clamps to the
  viewport independently.
- Viewport controls stay anchored to the full viewport (see
  `viewport-quick-controls-plan.md`), so a floating Layers panel may cover the left rail.
  That is accepted: controls do not move to avoid overlays.
- Out of scope: dragging a panel between columns, docking to the opposite side, and
  persistence.

## Visibility state model (Auto mode — deferred until the cycle ships)

| State | Overlay |
|---|---|
| Selection exists | Shown — selection content |
| No selection + `P` pressed (manual open) | Shown — GLOBAL section |
| No selection + not manually opened | Hidden |
| Pin active | Always shown; auto-hide suppressed |

- `P` toggles manual visibility; Pin (in the header) suppresses auto-hide.
- Undo clears selection → overlay would hide on every undo. Decide: keep
  selection on undo, or accept the hide.
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

Auto-hide interaction: hide only when no selection **and** the variables group
is collapsed; or drop auto-hide in favor of manual `P` + collapse.

## Draggable popover with vertical collapse to top

Assessment: **medium, not painful.**

| Piece | Size | Notes |
|---|---|---|
| Overlay placement | Small | Audit §6 option B |
| Collapse to top | Small | Height override → header row; the identity row (L396–405) already works as the collapsed bar; instant, matching the gallery collapse |
| Draggable position | Medium | The only genuinely new machinery — no in-repo precedent |
| Resize | Small–medium | One edge handle, mouse capture + delta (slider pattern) |
| Persistence | Small | Position, size, collapsed → `UMixtormatEditorSettings` (additive) |

### The drag is the real work

- Standard tool: `SConstraintCanvas` (anchors + offsets) inside the existing
  `SOverlay` slot in `BuildPreviewPanel` (`SMixtormat_Preview.cpp` L1520–1631).
- Drag handle = the header row; mouse capture + delta.
- Clamp to viewport bounds, and re-clamp on window resize.
- Position/size/collapsed state on `SMixtormat` so it survives theme
  reconstruction.

### Risks

- Theme reconstruct must re-apply position/size (state on the widget — fine).
- New metrics (popover min/max size, header height) need tokens + schema
  entries per the no-local-styling rule.
- Hit-testing is fine: the overlay only blocks where it is; camera orbit is
  unaffected elsewhere.
- Popup boundary: menus opened from the overlay clamp to the SDockTab rect
  (`SMixtormatPopupAnchor.cpp` L108–115) — unaffected.

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
| Variables pinned group | Small (part of the variables plan) |
