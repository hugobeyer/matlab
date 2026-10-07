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
| D19 | Viewport marking menu is invoked with `Q`; RMB click is reserved for the later context menu, and RMB drag keeps rotating lighting. **Amended from Tab (I13):** Slate navigates focus on Tab regardless of a widget handling it, so Tab also moved focus to the top bar | User; implemented (I11), key amended (I13) |
| D20 | Inspector overlay height auto-fits its content (capped at the viewport); a top/bottom corner drag makes it explicit, and foldout collapse then leaves the size alone | User |
| D21 | The overlay is placed inset from the viewport edges by a token on first entry, not flush | User |
| D22 | Superseded by D30–D31: left navigation/content use the pinned-rail shared-overlay model, not a left Docked/Overlay/Hidden splitter column | Superseded by the user's overlay-only direction |
| D23 | Reopening a panel preserves its Auto/Explicit height mode and user-set height, subject to viewport clamping. Foldout collapse does not reset explicit height; a small Fit height action returns to Auto without resetting width/position | User approved recommendation; implemented in I7, needs validation |
| D24 | Superseded by D30–D32: left navigation/content are viewport overlays, not a shell splitter cell | Superseded by the user's overlay-only direction |
| D25 | Superseded by D32: the gallery becomes a bottom overlay drawer; no preview/gallery or materials/masks divider | Superseded by the user's overlay-only direction |
| D26 | Superseded by D19/I13: bare Q toggles the viewport quick controls; Tab remains normal Slate navigation | User; implemented in source, needs validation |
| D27 | Merge AA / Scale / Displacement with Default / Lumen / Final into the Render strip | User; implemented in I9/I10, needs validation |
| D28 | Quick-controls popup is an in-viewport overlay. Controls retain normal input focus/capture; nested Final menus remain usable. Close popup/nested menus and release capture before theme reconstruction | User approved recommendation; implemented in I11, needs validation |
| D29 | Implement in stages; user tests each implemented stage in-editor. Input delivery and UI behaviour are to be proven during implementation/testing, not assumed from a passing build | User |
| D30 | The left navigation rail is glued/pinned to the viewport edge as an overlay; it reserves no shell width and is not draggable | User confirmed the overlay recommendation |
| D31 | LAYERS, LIBRARY, and GLOBAL all open in one shared left overlay surface. Rail selection switches/replaces the active content; Layers alone can be dragged out and returned by clicking its rail icon or dragging it back to the rail. Keep one live instance per page; no left splitter/cell | User confirmed the overlay recommendation |
| D32 | The materials/masks gallery is one bottom overlay drawer with a mode switch. Remove the preview/gallery vertical splitter and the materials/masks internal splitter; no reserved gallery cell | User confirmed the overlay recommendation |

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
| I9 | Preview controls extracted into shared builders (`BuildPreview{Render,Lighting,Geometry,Scene,Camera,Output}Controls`); AA/Scale/Default-Lumen/Final/Displacement merged into one Render strip (D27); GLOBAL gains a PREVIEW / VIEWPORT section with five group-visibility switches and the same settings, organised as cards (VISIBILITY / RENDER / LIGHTING / GEOMETRY / CAMERA / OUTPUT) with the icon buttons laid out inline (`EPreviewControlLayout`); GLOBAL empty-state opacity tokenized | `SMixtormat_PreviewControls.cpp` (new), `SMixtormat_Preview.cpp` (shrank ~610 lines), `SMixtormat_Shell.cpp`, `SMixtormat.h`, `MixtormatDesignTokens.h` |
| I10 | Side resize grips: left/right edges (width-only, height stays auto and re-measures), corners unchanged; AA / Default-Lumen / displacement removed from the viewport overlay and kept in GLOBAL only, leaving the overlay strip as render Scale + Final | `SMixtormatOverlayPanel.*`, `SMixtormat_Overlays.cpp`, `SMixtormat_Inspector.cpp`, `SMixtormat_Shell.cpp`, `SMixtormat_PreviewControls.cpp`, `SMixtormat_Preview.cpp` |
| I11 | Quick controls (D26/D28): initially Tab, later changed to bare Q (I13), routed through `FMixtormatPreviewViewportClient::InputKey`; in-viewport popup around the pointer -- RENDER top, LIGHTING left, GEOMETRY right, disabled ACTIONS bottom; card reveal animation, centered after measurement and viewport-clamped. Escape/outside click/rebuild dismiss; Q also closes while popup controls own focus, and geometry/light selection auto-dismisses | `SMixtormatPreviewViewport.*`, `SMixtormat_PreviewControls.cpp`, `SMixtormat_Preview.cpp`, `SMixtormat_Overlays.cpp`, `SMixtormat.cpp`, `SMixtormat_Theme.cpp`, `SMixtormat.h`, `MixtormatDesignTokens.h` |
| I12 | Left column navigation is a vertical icon rail (LAYERS / LIBRARY / GLOBAL) that stays docked; only the layer stack pops out, so the floating panel is the stack plus its grab margin and grips. The rail's selection follows the cell: docking the stack selects LAYERS, and while it floats or is hidden the cell falls back to the last non-layers page; choosing LAYERS in the rail while it floats docks it back. `SMixtormatTabStrip` is still used by the UI STYLE panel, so nothing became dead | `UI/Controls/SMixtormatIconRail.*` (new), `SMixtormat_Shell.cpp`, `SMixtormat_Overlays.cpp`, `SMixtormat.h`, `MixtormatDesignTokens.h` |
| I13 | Quick controls moved from Tab to `Q`: Tab delivery worked, but Slate's focus navigation runs on Tab regardless of the widget handling it, so the top bar lit up. Popup spacing widened; a theme-tokenized hairline cross fades outward from a soft layered centre bloom behind the cards | `SMixtormatPreviewViewport.*`, `SMixtormat_Preview.cpp`, `SMixtormat_PreviewControls.cpp`, `MixtormatDesignTokens.h` |
| I14 | Revised workspace: Layers/Library/Global are a resizable left shell column; Layers alone reparent between home and a draggable/resizable pop-out, returning by rail click or snap-back. The gallery is one resizable bottom MATERIALS/MASKS drawer over the full workspace, with simultaneous material/mask columns and no gallery splitters. Inspector docking/overlay/hidden behavior remains unchanged. | `SMixtormat_Shell.cpp`, `SMixtormat_Preview.cpp`, `SMixtormat_Overlays.cpp`, `SMixtormat_Library.cpp`, `SMixtormat.h` |

I7–I14 are implemented in source and need in-editor validation. Variables and layout
persistence remain separate work.

**Tab delivery is settled (I13):** the client did receive Tab, but Slate's focus navigation runs
on Tab regardless of the event being handled, so the key moved to `Q`. Tab is only recoverable
with a pre-routing input processor, which would consume Tab editor-wide while the pointer is over
the viewport — not worth it for a viewport menu.

## Recommended (pending confirmation)

| # | Recommendation | Why |
|---|---|---|
| R1 | ~~Variables home = pinned collapsible GLOBAL group in the inspector~~ — superseded by D13 (GLOBAL left tab) | User chose the third left tab over the pinned group |
| R2 | ~~Gallery = tabs (Materials / Masks)~~ — adopted as the mode switch inside the single bottom drawer (D32) | Reuses existing builders; no internal splitter |
| R3 | Inspector header row (name, Add, Pin/Dock/Hide) | Pin/Dock/Hide need a home; reuse `LayerStackHeaderHeight` pattern |
| R4 | Malformed both-enabled binding: reference wins, variable driver skipped | Preserves existing behavior exactly; editor prevents authoring it |
| R5 | Effective value: authored on slider + driven badge; computed result in popover only | Consistent with spatial drivers; no slider write/reset confusion |
| R6 | Persistence scope: widths, heights, placement mode, collapsed flags only | Minimal; skip selection/scroll |
| R7 | ~~Auto-hide exception based on the inspector's variables group~~ — superseded by D13 | GLOBAL is in the left panel; Auto visibility remains deferred |
| R8 | ~~Inspector free drag: defer to a later pass~~ — superseded by D18 | Overlay + dock + hide + resize covers the need; drag is the only new machinery |

## Open

| # | Question | Notes |
|---|---|---|
| O1 | Gallery form | Resolved by D32: one bottom overlay drawer with MATERIALS/MASKS mode switch; both splitter divisions removed |
| O2 | Variables placement | Resolved — GLOBAL left tab (D13) |
| O3 | Malformed both-enabled precedence final confirm | R4 recommended |
| O4 | Effective-value display final confirm | R5 recommended |
| O5 | Inspector drag: commit or defer | Resolved — D18 superseded R8; the drag shipped (I6) |
| O6 | Future Auto-visibility behaviour after undo | `SMixtormat.cpp` L299–311 clears multi/group/effect/mask selection but retains/clamps a valid layer index; undo does not always remove inspector selection. No undo change in the current scope |
| O7 | Compact viewport mode (edge popovers + marking menu) | Q quick controls are implemented in source but need build/editor validation. Flick/release gestures remain deferred |
| O8 | `L` behavior for Layers | Reconcile during D31: define Docked-at-home / popped-out / hidden behavior and update label/help; don't retain misleading old cycle text |
| O9 | Which left content may pop out? | Resolved by D31: Layers alone; Library/Global remain pages in the shared overlay surface. The rail stays pinned |
| O10 | How does an explicitly-sized overlay return to auto-fit height? | Resolved — Fit height action (D23). Reopening/cycling preserves height mode; no automatic reset |
| O11 | Overlay stacking/fronting | Inspector, one left overlay and gallery may coexist; define deterministic hit/front order during D30–D32 implementation |
| O12 | Continuous viewport clamping | Implemented in I7 via overlay layout evaluation; re-test after D30–D32 remove/change splitter geometry |

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

1. Rebuild and verify the latest rail and quick-controls edits before changing layout.
2. Implement D30: remove left shell slot; pin rail over viewport with no reserved width.
3. Implement D31: single shared left overlay; Layers drag-out/return; preserve all page state.
4. Implement D32: bottom gallery overlay and MATERIALS/MASKS mode switch; remove both gallery splitters.
5. Add the approved UI STYLE metrics in `overlay-workspace-handoff.md` to the existing Preview/Gallery theme groups; structural limits remain tokens.
6. Re-test Inspector placement/fronting and workspace reconstruction; persistence is separate.

Variables data-model work and persistence are independent. See
`overlay-workspace-handoff.md` for source paths, migration details and the test matrix.
