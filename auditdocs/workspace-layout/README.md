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
| `ui-layout-and-panels-audit.md` | Audit: gallery split, panel redocking, new cell, collapse toggles, inspector overlay |
| `global_variables_plan.md` | Implementation plan: document-scope variable table driving float parameters |
| `inspector-placement-model.md` | Inspector as overlay / docked / hidden / auto, draggable popover, collapse-to-top |
| `decisions-log.md` | Decided / recommended / open / rejected / implemented, with reasons |
| `inspector-popover-handoff.md` | Handoff prompt: inspector Docked → Overlay → Hidden cycle |
| `viewport-quick-controls-plan.md` | Source audit and lean directional popup / GLOBAL visibility plan |
| `mixtormat_mermaid_concepts.md` | Original hybrid-workspace concept diagrams + codebase reconciliation |
| `mermaid-diagrams.md` | Decision flowcharts and the inspector visibility state model |

## Key facts (verified against source)

- Shell tree: `SVerticalBox`[TopBar / `MainSwitcher` / StatusBar] →
  `BuildAuthoringPage()` = horizontal splitter [Left | Center | Inspector];
  Center = vertical splitter [Preview / BottomLibrary]; BottomLibrary =
  horizontal splitter [MATERIALS | MASKS].
- Shell split fractions and overlay geometry are volatile state on `SMixtormat`;
  workspace layout is not persisted to disk.
- Reconstruct/StyleRefresh changes rebuild the workspace; Paint/Layout requests
  invalidate directly (`SMixtormat_Theme.cpp` L139–164). During reconstruction,
  `TransferLayoutState` carries
  scroll offsets and inspector-group expansion; state held on the retained
  `SMixtormat` (split fractions, overlay geometry, preview values) also survives.
  These workspace layout choices are not persisted to disk (theme data is separate).
- The whole workspace is one NomadTab; no internal docking exists.
- GLOBAL is now the **third left-column tab** (`BuildGlobalPage` in
  `SMixtormat_Shell.cpp`); the inspector placeholder was removed.
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
- Variables home: GLOBAL third left-column tab (decided, D13; placeholder
  implemented). The inspector-pinned group idea was superseded.
- Inspector: Docked / Overlay / Hidden; `P` cycles all three (D15). Overlay is
  borderless and square-cornered (D16). Auto mode deferred. Clamping runs on entry
  and during drag/resize only — a re-clamp on viewport resize is open (O12) — and
  D21's panel inset must be tokenized. A dedicated `InspectorOverlayInset` is
  recommended to avoid coupling panel defaults to toolbar spacing.
- Layout persistence: `UMixtormatEditorSettings` (additive config), never the
  theme store.
- Hotkeys: `G` exists; `L` and `P` implemented workspace-wide (D14) via a
  Slate input preprocessor.
- Implemented this session: collapse toggles + guards, GLOBAL tab, workspace
  hotkeys, prototype geometry sync, and the inspector placement cycle with
  overlay drag/resize — see `decisions-log.md` §Implemented.

## Implementation readiness

Design choices are recorded in D23–D29; ready for staged implementation, **not a
claim of validated UI behaviour**. The prior build passed according to the user,
but no in-editor behaviour is validated. Tab delivery will be proven during the
routing stage; the user will test each stage after it is implemented.

- Start with token corrections and shared-builder/controller boundaries, preserving
  existing values, callbacks and one live instance of each panel.
- D20/D21/D23: reopen with remembered height/mode; Fit height returns to Auto.
  Prove measurement, bounded scrolling and re-clamping; do not add Auto visibility.
- D24/D25: float the whole left panel; `L` cycles placement; clicked floating
  panels come to front. Define minimum sizes and validate both-sided write-back.
- D26–D28: press-to-open Tab, merged Render strip, in-viewport popup with normal
  control focus/nested Final menus. Close before theme rebuilding. Fix the builder
  API before extraction; hidden groups retain existing hotkeys/settings.
- Gallery popover/auto-collapse remains deferred; keep the bottom-docked toggle.
- D29: implement, then the user tests each stage: placement, Fit height, four grips,
  splitters, text entry, viewport input, nested menus and reconstruction. Agent-side
  build/editor runs still require authorization; this documentation pass uses neither.

Decision and implementation sequences are in the two focused plans. Global variables,
gallery changes and persistence are independent work; do not silently include them.

## Provenance

- `ui-layout-and-panels-audit.md` — moved from `auditdocs/`.
- `global_variables_plan.md` — moved from `AgentDocs/code_docs/` (it was the
  plan of record there; this folder is now its home).
- `mixtormat_mermaid_concepts.md` — moved from `auditdocs/`, with a
  reconciliation section appended.
