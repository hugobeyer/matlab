# Workspace Layout & Global Variables — Design Folder

Status: design and planning only. Source wins over every document here;
verify line numbers before acting. See `decisions-log.md` §Implemented for
what has since landed in source.

This folder consolidates the workspace-layout and global-variables design work:
the audits, the implementation plan, the inspector placement model, the
decision log, and the diagrams.

## Contents

| File | What it is |
|---|---|
| `ui-layout-and-panels-audit.md` | Current audit/decision for pinned left rail, shared left overlays, bottom gallery drawer, and Inspector coexistence |
| `global_variables_plan.md` | Implementation plan: document-scope variable table driving float parameters |
| `inspector-placement-model.md` | Inspector as overlay / docked / hidden / auto, draggable popover, collapse-to-top |
| `decisions-log.md` | Decided / recommended / open / rejected / implemented, with reasons |
| `inspector-popover-handoff.md` | Handoff prompt: inspector Docked → Overlay → Hidden cycle |
| `viewport-quick-controls-plan.md` | Source audit and quick-controls popup behavior / GLOBAL visibility plan |
| `overlay-workspace-handoff.md` | Next-chat implementation brief, source map, UI STYLE token checklist, and test matrix |
| `mixtormat_mermaid_concepts.md` | Original hybrid-workspace concept diagrams + codebase reconciliation |
| `mermaid-diagrams.md` | Decision flowcharts and the inspector visibility state model |

## Key facts (verified against source)

- **Current source (before D30–D32):** `BuildAuthoringPage()` uses a horizontal
  splitter [Left | Preview+BottomLibrary | Inspector], with vertical Preview / Gallery
  and horizontal MATERIALS / MASKS splitters. D30–D32 approve removing the left and
  gallery cells in favor of a pinned overlay rail, one shared left-content overlay,
  and one bottom gallery drawer. This layout change is not implemented yet.
- Shell split fractions and overlay geometry are volatile state on `SMixtormat`;
  workspace layout is not persisted to disk.
- Reconstruct/StyleRefresh changes rebuild the workspace; Paint/Layout requests
  invalidate directly (`SMixtormat_Theme.cpp` L139–164). During reconstruction,
  `TransferLayoutState` carries
  scroll offsets and inspector-group expansion; state held on the retained
  `SMixtormat` (split fractions, overlay geometry, preview values) also survives.
  These workspace layout choices are not persisted to disk (theme data is separate).
- The whole workspace is one NomadTab; no internal docking exists.
- Current source has GLOBAL as the **third left-cell page** (`BuildGlobalPage` in
  `SMixtormat_Shell.cpp`); D31 moves this page into the shared left overlay surface.
  The inspector placeholder was removed.
- `L` / `P` use a Slate input preprocessor with repeat/modifier guards and a
  focused-widget type check for text entry. The source has no workspace/tab/window
  ownership gate (`SMixtormat.cpp` L30–48, L69–88); do not mistake application-wide
  registration for verified workspace-only routing.
- Global variables have **no runtime concept today**; the parameter
  binding/driver system is the integration point.
- `Config/MixtormatParameterAuthoring.json` is editor authoring metadata
  (defaults/ranges/snaps), **not** a binding store.

## Key decisions at a glance

- Global variables: float-only v1, reuse `FMixtormatParameterDriver` +
  `SourceVariableId`, resolve CPU-side before gather, no shader changes.
- Variables UI is the GLOBAL page (D13; placeholder implemented), currently the
  third left-cell page. D31 moves it into the shared left overlay surface. The
  inspector-pinned group idea was superseded.
- Inspector: Docked / Overlay / Hidden; `P` cycles all three (D15). Overlay is
  borderless and square-cornered (D16). Auto visibility is deferred. Overlay geometry
  re-clamps during layout (I7); re-test after the approved shell layout changes.
- Layout persistence: `UMixtormatEditorSettings` (additive config), never the
  theme store.
- Hotkeys: `G` exists; `L` and `P` implemented workspace-wide (D14) via a
  Slate input preprocessor.
- Implemented in source: GLOBAL page, `L`/`P`, Inspector placement, left-layer overlay,
  and viewport quick controls. **Next approved stage:** D30–D32 remove left and gallery
  splitter cells; see `overlay-workspace-handoff.md` before implementation.

## Implementation readiness

The current approved workspace direction is D30–D32. Earlier D23–D29 describe
implemented overlay infrastructure and viewport controls; they are **not a claim of
validated UI behaviour**. Recent user builds reported compile errors; local fixes have
not yet been followed by a reported successful rebuild. Q delivery and the quick-controls
guide need viewport verification.

- Start with token corrections and shared-builder/controller boundaries, preserving
  existing values, callbacks and one live instance of each panel.
- D20/D21/D23: reopen with remembered height/mode; Fit height returns to Auto.
  Prove measurement, bounded scrolling and re-clamping; do not add Auto visibility.
- D30–D32: pinned viewport-edge rail; shared left overlay for Layers/Library/Global;
  Layers can drag out and return to the rail; one bottom gallery drawer with a
  MATERIALS/MASKS mode switch. Remove the associated splitter cells and fractions.
- Quick controls use `Q` (not Tab); Q/Escape/outside click and geometry/light selection
  dismiss the popup. Preserve focus, nested menus, and slider interaction.
- D29: implement in stages, then the user tests each stage: placement, Fit height,
  grips, remaining Inspector splitter, text entry, viewport input, nested menus and
  reconstruction. Agent-side build/editor runs still require authorization.

Implementation details, token checklist, test matrix and handoff are in
`overlay-workspace-handoff.md`. Keep GLOBAL's existing document variables/settings;
this stage changes their surface placement, not their data model. Persistence remains
separate and must not be silently added.

## Provenance

- `ui-layout-and-panels-audit.md` — moved from `auditdocs/`.
- `global_variables_plan.md` — moved from `AgentDocs/code_docs/` (it was the
  plan of record there; this folder is now its home).
- `mixtormat_mermaid_concepts.md` — moved from `auditdocs/`, with a
  reconciliation section appended.
