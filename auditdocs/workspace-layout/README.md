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
- All split fractions are volatile floats on `SMixtormat` — **no persistence
  exists** anywhere.
- Theme refresh does a full workspace reconstruction; only scroll offsets and
  inspector-group expansion survive (`TransferLayoutState`).
- The whole workspace is one NomadTab; no internal docking exists.
- GLOBAL is now the **third left-column tab** (`BuildGlobalPage` in
  `SMixtormat_Shell.cpp`); the inspector placeholder was removed.
- `L` / `P` are routed through a Slate input preprocessor, so they fire
  regardless of the focused widget (text entry keeps priority).
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
  borderless and square-cornered (D16). Auto mode deferred.
- Layout persistence: `UMixtormatEditorSettings` (additive config), never the
  theme store.
- Hotkeys: `G` exists; `L` and `P` implemented workspace-wide (D14) via a
  Slate input preprocessor.
- Implemented this session: collapse toggles + guards, GLOBAL tab, workspace
  hotkeys, prototype geometry sync — see `decisions-log.md` §Implemented.

## Provenance

- `ui-layout-and-panels-audit.md` — moved from `auditdocs/`.
- `global_variables_plan.md` — moved from `AgentDocs/code_docs/` (it was the
  plan of record there; this folder is now its home).
- `mixtormat_mermaid_concepts.md` — moved from `auditdocs/`, with a
  reconciliation section appended.
