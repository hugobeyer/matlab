# Inspector Placement Model

Extends `ui-layout-and-panels-audit.md` §6. Status: design, no code written.

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
  - **GLOBAL placeholder group** — L412–430, visible when
    `bHasWorkingMaterial && !HasAnySelection()`, currently reads
    "No global settings yet."
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
| Overlay | Floats over the viewport, resizable from its left edge |
| Hidden | Collapsed away entirely |
| Auto | Overlay shown on selection, hidden otherwise (see state model) |

Implementation follows audit §6 option B: one instance, placement decided in
`BuildWorkspaceUI`; never two instances (duplicate scroll/expansion state).

## Visibility state model (Auto mode)

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

## The GLOBAL section is the variables home

The placeholder at L412–430 was built as the document-scope slot. It sits
**outside** the scroll boxes, so it is pinned at the top of the inspector —
exactly where a variable scrub target belongs.

Recommended: replace the placeholder with the global variables cell
(collapsible `SMixtormatInspectorGroup`, expansion preserved by
`TransferLayoutState`), cap the list height with its own scroll (precedent:
`InspectorMaskGalleryMaxHeight` token, `MixtormatDesignTokens.h` L616).

## Showing variables and selection content at the same time

The driving workflow is: scrub a variable, watch a driven parameter change. That
requires variables and selection content to coexist.

| Where | Both visible? | Cost |
|---|---|---|
| Pinned group in inspector (recommended) | Yes — variables on top, selection scrolls below | Smallest; reuses the existing slot + collapse machinery |
| Bottom drawer third tab (Materials / Masks / Variables) | Yes — full width for sliders | Small; drawer eats viewport height, often collapsed |
| Left panel third tab | Yes, but loses the layer stack while editing variables | Small; worst for the driving workflow |
| Separate overlay / floating window | Yes | Most chrome; fights the "max viewport" goal |

With the pinned group, the full driving layout is: layer stack (left) +
viewport (center) + variables and driven parameters (right), all visible.

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
