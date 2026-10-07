# Inspector Popover Cycle — Handoff

Status: ready to implement. Decisions: D15 (cycle), D16 (overlay styling).
Design context: `inspector-placement-model.md`. History: `decisions-log.md`
§Implemented for what already exists.

## Prompt

Implement an Inspector placement cycle in the Mixtormat Unreal Editor plugin.

**Goal.** `P` cycles the inspector through **Docked → Overlay → Hidden →
Docked**. Overlay floats the same inspector over the viewport's right edge.
This is a prototype to test whether the overlay workflow beats the docked
column before adding anything heavier.

**Constraints.**
- One inspector instance only — never build a second; scroll and expanded-group
  state must not diverge (X7 rejects the two-instance shortcut).
- Overlay styling: no outer border, no rounded corners. Inner components keep
  their existing theme styling — tokens only, no local literals. Today the
  root is `SBox.WidthOverride(MixtormatTokens::InspectorWidth)` wrapping an
  `SBorder` with the rounded `Mixtormat.Panel` brush
  (`SMixtormat_Inspector.cpp` ~L353; brush in `MixtormatStyle.cpp` ~L165,
  `ControlLayout.CornerRadius`) — the overlay path needs a square, borderless
  variant.
- `L` stays on the Layers/Library panel. `P` currently collapses the
  inspector (`ToggleInspectorCollapsed`, called from both `OnKeyDown` and the
  workspace input preprocessor in `SMixtormat.cpp`) — that handler becomes the
  cycle.
- Placement state lives on `SMixtormat` so theme reconstruction preserves it;
  re-check `TransferLayoutState()` in `SMixtormat_Theme.cpp`.
- Don't cover viewport controls where practical (right-centre
  `GeometryControls`, top-right light gizmo).
- Out of scope: dragging, resizing, persistence, auto-hide, marking menu,
  gallery changes, GLOBAL tab changes.

**Files to inspect first.**
- `Source/MixtormatEditor/Private/Widgets/SMixtormat.h` — state, hotkey member
- `.../SMixtormat.cpp` — `Construct`, input preprocessor, `OnKeyDown`
- `.../SMixtormat_Shell.cpp` — right shell slot (`BuildAuthoringPage`),
  top-bar Show/Hide buttons, `ToggleInspectorCollapsed`
- `.../SMixtormat_Preview.cpp` — `BuildPreviewPanel` overlay slots
- `.../SMixtormat_Inspector.cpp` — `BuildInspectorPanel` root
- `.../SMixtormat_Theme.cpp` — `ApplyPendingTheme`, `TransferLayoutState`
- `AgentDocs/UI.md`; this folder's `inspector-placement-model.md`

**Acceptance.**
- `P` and a visible placement control both cycle the three states.
- Docked and Hidden behave as today; Overlay shows the same inspector,
  borderless and square-cornered, over the viewport's right edge.
- Theme refresh (LiveTheme / Reconstruct) does not reset the placement.
- Source wiring verified; report if no Unreal build/UI test was possible.
