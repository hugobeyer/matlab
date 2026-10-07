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
| D19 | Viewport marking menu is invoked with Tab; RMB click is reserved for the later context menu, and RMB drag keeps rotating lighting | User |
| D20 | Inspector overlay height auto-fits its content (capped at the viewport); a top/bottom corner drag makes it explicit, and foldout collapse then leaves the size alone | User |
| D21 | The overlay is placed inset from the viewport edges by a token on first entry, not flush | User |
| D22 | The left panel gets the same placement model as the inspector: Docked → Overlay → Hidden, one instance, draggable and resizable | User |
| D23 | Reopening a panel preserves its Auto/Explicit height mode and user-set height, subject to viewport clamping. Foldout collapse does not reset explicit height; a small Fit height action returns to Auto without resetting width/position | User approved recommendations; planned, not implemented |
| D24 | Float the entire LAYERS / LIBRARY / GLOBAL panel including its tab strip; `L` cycles Docked → Overlay → Hidden → Docked | User approved recommendations; planned, not implemented |
| D25 | Clicked floating panels come to front; maintain independent geometry and gesture state. Gallery stays bottom-docked with its existing toggle; gallery-popover/auto-collapse work is deferred | User approved recommendations; planned, not implemented |
| D26 | Bare Tab presses once to open; release does nothing. Escape/outside click dismisses; no hold/flick/release gesture in v1 | User; planned, not implemented |
| D27 | Merge AA / Scale / Displacement with Default / Lumen / Final into the Render strip | User; planned, not implemented |
| D28 | Quick-controls popup is an in-viewport overlay. Controls retain normal input focus/capture; nested Final menus remain usable. Close popup/nested menus and release capture before theme reconstruction | User approved recommendation; planned, not implemented |
| D29 | Implement in stages; user tests each implemented stage in-editor. Tab delivery and UI behaviour are to be proven during implementation/testing, not assumed from a passing build | User |

## Implemented (2026-10)

| # | Item | Files |
|---|---|---|
| I1 | Layers/Inspector collapse: top-bar Hide/Show buttons, remembered widths, split write-back guards | `Source/MixtormatEditor/Private/Widgets/SMixtormat_Shell.cpp`, `SMixtormat.h` |
| I2 | `L` / `P` workspace-wide: Slate input preprocessor; ignores repeats and modifiers; yields to text entry; unregistered on close | `SMixtormat.cpp`, `SMixtormat_Theme.cpp` |
| I3 | GLOBAL as third left tab; inspector placeholder removed | `SMixtormat_Shell.cpp` (`BuildGlobalPage`), `SMixtormat_Inspector.cpp` |
| I4 | Prototype geometry synced from Unreal: left 423 / inspector 520 / top bar 34 / layer row 26 / status bar 24 | `Docs/mixtormat-ui-prototype.html` |
| I5 | `Docs/ui-prototype/` removed from the AGENTS.md exclusion list; the Zed `file_scan_exclusions` still blocks agent tooling — un-exclude in `.zed/settings.json` to read `tokens.css` / `components.css` / `falloff.js` | `AGENTS.md`, `.zed/settings.json` |
| I6 | Inspector placement cycle: `P` + top-bar control cycle Docked → Overlay → Hidden; one `InspectorPanel` reparented between dock and viewport hosts; overlay dragged by the identity row, four corner resize grips, clamped on entry and during drag/resize | `SMixtormat_Shell.cpp`, `SMixtormat_Inspector.cpp`, `SMixtormat_Preview.cpp`, `SMixtormat.h` |
| I7 | Overlay auto-fit height (capped at the viewport, explicit mode retained across reopen, foldout collapse never resets it), tokenized first-entry inset, Fit height action (bottom-centre chevron, shown only while the height is explicit), and re-clamp on every layout pass (window/splitter/gallery) | `SMixtormatOverlayPanel.*` (new), `SMixtormat_Overlays.cpp` (new), `SMixtormat_Inspector.cpp`, `MixtormatDesignTokens.h`, `SMixtormat.h` |
| I8 | Left panel placement: `L` cycles Docked → Overlay → Hidden → Docked; one `LeftPanel` (whole panel incl. tab strip) reparented between dock and viewport hosts; drag by an empty grab margin above the tab strip (no header row); shared drag/resize machinery with independent geometry; symmetric splitter write-back; clicked floating panel comes to front | `SMixtormat_Overlays.cpp`, `SMixtormat_Shell.cpp`, `SMixtormat.h` |

I7/I8 are **code-complete and untested**: no build or in-editor run has been made in this
session, and the repo's language-server diagnostics do not resolve engine/plugin include paths,
so they carry no signal here. Gallery stays bottom-docked; variables, persistence, Auto
visibility and collapse-to-header remain out of scope.

## Recommended (pending confirmation)

| # | Recommendation | Why |
|---|---|---|
| R1 | ~~Variables home = pinned collapsible GLOBAL group in the inspector~~ — superseded by D13 (GLOBAL left tab) | User chose the third left tab over the pinned group |
| R2 | Gallery = tabs (Materials / Masks) | Reuses `SMixtormatTabStrip`; full-width grids; kills two fraction states |
| R3 | Inspector header row (name, Add, Pin/Dock/Hide) | Pin/Dock/Hide need a home; reuse `LayerStackHeaderHeight` pattern |
| R4 | Malformed both-enabled binding: reference wins, variable driver skipped | Preserves existing behavior exactly; editor prevents authoring it |
| R5 | Effective value: authored on slider + driven badge; computed result in popover only | Consistent with spatial drivers; no slider write/reset confusion |
| R6 | Persistence scope: widths, heights, placement mode, collapsed flags only | Minimal; skip selection/scroll |
| R7 | ~~Auto-hide exception based on the inspector's variables group~~ — superseded by D13 | GLOBAL is in the left panel; Auto visibility remains deferred |
| R8 | ~~Inspector free drag: defer to a later pass~~ — superseded by D18 | Overlay + dock + hide + resize covers the need; drag is the only new machinery |

## Open

| # | Question | Notes |
|---|---|---|
| O1 | Gallery form: tabs vs vertical split vs keep splitter | Three competing resolutions; R2 recommended |
| O2 | Variables placement | Resolved — GLOBAL left tab (D13) |
| O3 | Malformed both-enabled precedence final confirm | R4 recommended |
| O4 | Effective-value display final confirm | R5 recommended |
| O5 | Inspector drag: commit or defer | Resolved — D18 superseded R8; the drag shipped (I6) |
| O6 | Future Auto-visibility behaviour after undo | `SMixtormat.cpp` L299–311 clears multi/group/effect/mask selection but retains/clamps a valid layer index; undo does not always remove inspector selection. No undo change in the current scope |
| O7 | Compact viewport mode (edge popovers + marking menu) | The inspector cycle shipped (I6); the quick-controls prototype is now planned in `viewport-quick-controls-plan.md`, not implemented. Flick/release gestures remain deferred |
| O8 | Does `L` become the placement cycle for the left panel? | Resolved — D24; update label/tooltips/help with the implementation |
| O9 | Does only the LAYERS tab float, or the whole left panel with its tab strip? | Resolved — the whole panel travels (D24), still one instance |
| O10 | How does an explicitly-sized overlay return to auto-fit height? | Resolved — Fit height action (D23). Reopening/cycling preserves height mode; no automatic reset |
| O11 | Overlay stacking/fronting when two panels float (D22) | Resolved — clicked floating panel comes to front (D25). Capture routing remains implementation work |
| O12 | Missing continuous viewport clamping | D18 requires viewport clamping, but source clamps only on entry/drag (Shell L698–750, L807). Implement re-clamping for window/splitter/gallery changes; do not treat the requirement as an optional new feature |

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

1. ~~Inspector placement cycle — Docked → Overlay → Hidden~~ — done, see I6.
2. Inspector overlay auto-fit height + default inset (D20/D21), including the
   auto/explicit height mode (O10) and the viewport-resize re-clamp (O12).
3. Layers placement (D22). Prerequisites: extract a generic overlay controller
   from the inspector's drag/resize state (inspector-specific today), give the
   left column placement state of its own, and apply whole-panel scope/fronting (D24/D25).
4. Variables Phase 1–2 behind the GLOBAL tab (data model first; still the
   must-have feature, independent of layout work).
5. Gallery work only after O1/R2 is confirmed; finish Unreal → prototype geometry sync.
6. Layout persistence in editor settings (`UMixtormatEditorSettings`).
7. Viewport quick controls: settle the extraction API first; tokenize touched
   literals without changing appearance, extract builders, then visibility/regrouping
   and the Tab popup (see the focused plan's separate decision gates).

This is a suggested feature order, not a dependency chain: variables, gallery and
persistence are not prerequisites for quick controls. Recommendations and open
questions above are not user-approved decisions. See README §Implementation readiness
before starting a feature stage.
