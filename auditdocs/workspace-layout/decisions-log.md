# Decisions Log

Status: living document. "Decided" means the user stated it or the codebase
proves it. "Recommended" means proposed, awaiting confirmation. "Open" means
unresolved. "Rejected" means considered and dropped, with the reason.

## Decided

| # | Decision | Source |
|---|---|---|
| D1 | Global variables are a must-have feature | User |
| D2 | Variables are named floats with sliders that drive float parameters (opacity, effect amounts, generator values) | User |
| D3 | Variables and selection content must be viewable at the same time | User |
| D4 | Float-only v1; int/bool/enum later | Plan §1 |
| D5 | Reuse `FMixtormatParameterDriver` + new `SourceVariableId` field; no new address owner type | Plan §1, verified |
| D6 | Resolve CPU-side in the compositor layer copy, before hash + gather; no `.usf`/`.ush` changes | Plan §6, verified |
| D7 | Driver math must match `MixtormatDriver.ush` exactly (saturate → invert → remap → combine → Amount) | Plan §1, verified |
| D8 | Variable data lives in the `UMixtormatMaterial` asset, never the authoring JSON | Plan §11, verified |
| D9 | Layout persistence goes in `UMixtormatEditorSettings`, never the theme store | Audit §5, plan §3 |
| D10 | Hotkeys: `G` exists; `L` (layers) and `P` (inspector) are free | Audit §1, verified |
| D11 | Undo history must include the variable table (`FEditHistoryState`) | Plan §7 |
| D12 | Import of compositions with driven layers must not silently lose behavior | Plan §9 |

## Recommended (pending confirmation)

| # | Recommendation | Why |
|---|---|---|
| R1 | Variables home = pinned collapsible GLOBAL group in the inspector | Placeholder already exists (L412–430), pinned outside the scroll box; keeps left panel to Layers/Library |
| R2 | Gallery = tabs (Materials / Masks) | Reuses `SMixtormatTabStrip`; full-width grids; kills two fraction states |
| R3 | Inspector header row (name, Add, Pin/Dock/Hide) | Pin/Dock/Hide need a home; reuse `LayerStackHeaderHeight` pattern |
| R4 | Malformed both-enabled binding: reference wins, variable driver skipped | Preserves existing behavior exactly; editor prevents authoring it |
| R5 | Effective value: authored on slider + driven badge; computed result in popover only | Consistent with spatial drivers; no slider write/reset confusion |
| R6 | Persistence scope: widths, heights, placement mode, collapsed flags only | Minimal; skip selection/scroll |
| R7 | Auto-hide exception: hide only when no selection AND variables group collapsed | Keeps the variables cell reachable |
| R8 | Inspector free drag: defer to a later pass | Overlay + dock + hide + resize covers the need; drag is the only new machinery |

## Open

| # | Question | Notes |
|---|---|---|
| O1 | Gallery form: tabs vs vertical split vs keep splitter | Three competing resolutions; R2 recommended |
| O2 | Variables placement final confirm | R1 recommended |
| O3 | Malformed both-enabled precedence final confirm | R4 recommended |
| O4 | Effective-value display final confirm | R5 recommended |
| O5 | Inspector drag: commit or defer | Assessed medium; R8 recommends defer |
| O6 | Undo behavior: keep selection on undo, or accept overlay hiding | `ApplyEditHistoryState` L245 clears selection today |

## Rejected

| # | Option | Reason |
|---|---|---|
| X1 | GPU texture slots for variables | Only 2 fixed slots, spatial mask/region signals only |
| X2 | Editor-side variable resolution | Two resolution points; bake diverges; referenced compositions still need the compositor |
| X3 | Authoring JSON as a binding store | It is defaults/ranges/snaps metadata; bindings are reflected UPROPERTYs in the asset |
| X4 | Silent drop of variable-driven layers on import | Loses behavior without telling the user |
| X5 | Theme store for layout persistence | `FMixtormatShellMetrics` deliberately excludes layout |
| X6 | `FTabManager` nested docking | Heavy; panels are builders, not spawners; fights reconstruction |
| X7 | Second inspector instance for the overlay | Duplicates a huge tree; two scroll/expansion states diverge |
| X8 | Invented CPU driver chain | Preview and bake would drift from `MixtormatDriver.ush` |

## Suggested order

1. Variables Phase 1–2 (must-have; independent of layout work).
2. Collapse toggles + hotkeys + persistence (small, immediate value).
3. Gallery tabs.
4. Inspector overlay + header + collapse-to-top.
5. Inspector drag/resize (if still wanted).
