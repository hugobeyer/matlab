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
| D10 | Hotkeys: `G` exists; `L` (layers) and `P` (inspector) were free | Audit §1, verified — implemented, see I2 |
| D11 | Undo history must include the variable table (`FEditHistoryState`) | Plan §7 |
| D12 | Import of compositions with driven layers must not silently lose behavior | Plan §9 |
| D13 | Variables home is the GLOBAL third left-column tab (LAYERS / LIBRARY / GLOBAL); the inspector placeholder is removed | User; implemented (I3) |
| D14 | `L` and `P` are workspace-wide hotkeys: routed through a Slate input preprocessor so focus does not matter, yielding to text entry | User; implemented (I2) |
| D15 | `P` cycles inspector placement: Docked → Overlay → Hidden → Docked | User |
| D16 | Inspector overlay: no outer border and no rounded corners; inner component styling unchanged | User |
| D17 | Prototype sync direction for layout geometry is Unreal → HTML/CSS | User |
| D18 | Inspector overlay is draggable by its header row and resizable from all four corners, with hover-only L outlines and viewport clamping | User |

## Implemented (2026-10)

| # | Item | Files |
|---|---|---|
| I1 | Layers/Inspector collapse: top-bar Hide/Show buttons, remembered widths, split write-back guards | `Source/MixtormatEditor/Private/Widgets/SMixtormat_Shell.cpp`, `SMixtormat.h` |
| I2 | `L` / `P` workspace-wide: Slate input preprocessor; ignores repeats and modifiers; yields to text entry; unregistered on close | `SMixtormat.cpp`, `SMixtormat_Theme.cpp` |
| I3 | GLOBAL as third left tab; inspector placeholder removed | `SMixtormat_Shell.cpp` (`BuildGlobalPage`), `SMixtormat_Inspector.cpp` |
| I4 | Prototype geometry synced from Unreal: left 423 / inspector 520 / top bar 34 / layer row 26 / status bar 24 | `Docs/mixtormat-ui-prototype.html` |
| I5 | `Docs/ui-prototype/` removed from the AGENTS.md exclusion list; the Zed `file_scan_exclusions` still blocks agent tooling — un-exclude in `.zed/settings.json` to read `tokens.css` / `components.css` / `falloff.js` | `AGENTS.md`, `.zed/settings.json` |

## Recommended (pending confirmation)

| # | Recommendation | Why |
|---|---|---|
| R1 | ~~Variables home = pinned collapsible GLOBAL group in the inspector~~ — superseded by D13 (GLOBAL left tab) | User chose the third left tab over the pinned group |
| R2 | Gallery = tabs (Materials / Masks) | Reuses `SMixtormatTabStrip`; full-width grids; kills two fraction states |
| R3 | Inspector header row (name, Add, Pin/Dock/Hide) | Pin/Dock/Hide need a home; reuse `LayerStackHeaderHeight` pattern |
| R4 | Malformed both-enabled binding: reference wins, variable driver skipped | Preserves existing behavior exactly; editor prevents authoring it |
| R5 | Effective value: authored on slider + driven badge; computed result in popover only | Consistent with spatial drivers; no slider write/reset confusion |
| R6 | Persistence scope: widths, heights, placement mode, collapsed flags only | Minimal; skip selection/scroll |
| R7 | Auto-hide exception: hide only when no selection AND variables group collapsed | Keeps the variables cell reachable |
| R8 | ~~Inspector free drag: defer to a later pass~~ — superseded by D18 | Overlay + dock + hide + resize covers the need; drag is the only new machinery |

## Open

| # | Question | Notes |
|---|---|---|
| O1 | Gallery form: tabs vs vertical split vs keep splitter | Three competing resolutions; R2 recommended |
| O2 | Variables placement | Resolved — GLOBAL left tab (D13) |
| O3 | Malformed both-enabled precedence final confirm | R4 recommended |
| O4 | Effective-value display final confirm | R5 recommended |
| O5 | Inspector drag: commit or defer | Assessed medium; R8 recommends defer |
| O6 | Undo behavior: keep selection on undo, or accept overlay hiding | `ApplyEditHistoryState` L245 clears selection today |
| O7 | Compact viewport mode (edge popovers + marking menu) | Deferred; prototype the inspector overlay cycle first |

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

1. Inspector placement cycle — Docked → Overlay → Hidden
   (`inspector-popover-handoff.md`).
2. Variables Phase 1–2 behind the GLOBAL tab (data model first; still the
   must-have feature, independent of layout work).
3. Gallery tabs; finish the Unreal → prototype geometry sync.
4. Layout persistence in editor settings (`UMixtormatEditorSettings`).
5. Inspector drag/resize (only if still wanted after the cycle).
