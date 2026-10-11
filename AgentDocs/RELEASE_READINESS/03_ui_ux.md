# Readiness 03 — UI/UX Consistency and Persistence

**Branch:** `feature/behavior-system-v2`  
**Report IDs:** `UI-01`, `UI-02`, `UI-04`, `UI-05`, `UI-06`, `UI-07`, `UI-08`  
**Status:** Not started. Read `AGENTS.md`, `AgentDocs/UI.md` and `AgentDocs/HELPERS.md`. Also follow UI Style tokens and the current Inspector conventions.

## UI-01 — Native tooltip violations (P1, confirmed source)

**Reported evidence:** Roughly 30 sites use `.ToolTipText`, `SetToolTipText` or raw `SToolTip`, including `MixtormatLayerHierarchy.cpp`, `SMixtormat_Library.cpp` and `SMixtormatStatusDot.cpp`.

- Locate remaining direct native-tooltip sites; restrict edits to visible plugin surfaces.
- Route through existing `SMixtormatHelp` / `MakeStyledToolTip` and shared timing/appearance tokens.
- Preserve dynamic tooltip text, hotkeys, disabled explanations and hover behavior.
- Do not introduce a new tooltip helper, local colors or standalone timing constants.

## UI-02 — Shared control tooltip contract (P1, confirmed source)

**Reported evidence:** `SMixtormatIconButton.h` accepts `_ToolTip`, but some callers use `.ToolTipText`; `SMixtormatLayerIcon` lacks a help argument.

- Switch the incorrect callers to the existing shared help API; extend `SMixtormatLayerIcon` minimally to accept help where needed.
- Update affected Inspector, preview and hierarchy controls without changing sizing, state or icon behavior.
- Preserve existing interactions and UI STYLE sourcing.

**Trace:** `SMixtormatIconButton.h`, `SMixtormat_Preview.cpp`, `MixtormatInspectorGroup.cpp`, `UI/Layers/SMixtormatLayerIcon.*`.

## UI-04 — Legacy Flow/Behavior Inspector gaps (P2, likely; Behavior unaudited)

**Reported evidence:** `EMixtormatBehaviorType` contains six values, but only five panels appear mounted; obsolete Flow children may fall through to the layer Inspector.

- Inspect current enum values, valid states, Inspector dispatch and creation paths; distinguish intentional deprecated entries from reachable authored types.
- If a reachable child has no Inspector, route it to the current supported panel or an explicit non-editable state.
- Do **not** recreate V2/Legacy duplicate UI, old source/target selectors or a new Behavior architecture.
- Record unresolved Behavior-specific facts for the separate focused audit instead of declaring them fixed.

## UI-05 — Hardcoded design literals (P2, confirmed examples)

- Focus only on literals encountered in the affected UI surfaces.
- Replace duplicate colors, spacing and opacity with existing `MixtormatDesignTokens` and `MixtormatThemeStore` metrics; add canonical tokens only when no suitable owner exists.
- Do not turn this task into a whole-UI redesign.

## UI-06 — Layout and tab state not persisted (P2, confirmed source)

- Inspect current save/load of Inspector placement, Inspector tab and gallery state.
- Extend the established persistence path for missing states without replacing layout behavior or resetting compiled defaults.
- Keep UI Style reset/load semantics separate from session layout persistence.

## UI-07 — Sources shelf reorder not available (P2, confirmed source)

- Determine whether shelf order is purely organizational as specified in `AgentDocs/COMPOSITION.md`; preserve the dependency-ordered evaluation independently.
- Reuse existing shelf row drag/reorder conventions if this remains a product priority.
- Changing visible order must not alter source identity, references or demand-gather execution.

## UI-08 — `I` hotkey undiscoverable (P2, confirmed source)

- Surface existing shortcut help through canonical styled help, command hints or the already established shortcut display.
- Do not add competing keyboard handling or alter the current shortcut meaning.

## UI rules

Keep the left-column Shell, child menu sequence, layer Inspector, gallery and context-menu behaviors intact. Reuse shared controls and tokenized styling; no duplicate dropdown-heavy or connection-based UI. No tests, compile, runtime verification or unrelated refactor.
