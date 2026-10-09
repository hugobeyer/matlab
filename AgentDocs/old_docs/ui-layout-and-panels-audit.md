# UI Layout & Panels Audit

Point-in-time audit (2026-10). Source wins over this document; verify line
numbers before acting. Scope: shell layout flexibility, panel collapse,
inspector placement, and a new "cell" for global variables/settings.

No code was changed for this audit.

---

## 1. Current shell architecture (verified facts)

### Widget tree

- `SMixtormat::BuildWorkspaceUI()` — `Widgets/SMixtormat.cpp` L48–77:
  `SVerticalBox` [TopBar (auto) / `MainSwitcher` (fill) / StatusBar (auto)].
- `MainSwitcher` is an `SWidgetSwitcher` with **one** slot: `BuildAuthoringPage()`.
  (Multi-page structure was anticipated but only one page exists.)
- `BuildAuthoringPage()` — `Widgets/SMixtormat_Shell.cpp` L331–460:
  horizontal `SSplitter` with three slots:
  - Left: `BuildLeftPanel()` (LAYERS/LIBRARY tab strip + `LeftSwitcher`)
  - Center: vertical `SSplitter` [Preview / BottomLibrary]
  - Right: `BuildInspectorPanel()`
- `BuildBottomLibrary()` — `Widgets/SMixtormat_Library.cpp` L456–494:
  horizontal `SSplitter` [MATERIALS column / MASKS column].
- `BuildMaskBar()` — `Widgets/Layers/MixtormatLayerChildren.cpp` L1524–1590:
  MASKS header row + toolbar button + `SWrapBox` gallery.
- `BuildPreviewPanel()` — `Widgets/SMixtormat_Preview.cpp` L1015+:
  `SOverlay` with the viewport at slot 0 and overlay clusters at alignments
  (L1520–1631), all gated by `bPreviewOverlayUiVisible`.

### Layout state (all volatile, none persisted)

Six floats on `SMixtormat` (`Widgets/SMixtormat.h` L1472–1485):

- `ShellLeftFraction`, `ShellCenterFraction`, `ShellRightFraction`
- `PreviewHeightFraction` (bottom library is derived: `1.0 - PreviewHeightFraction`)
- `MaterialLibraryFraction`, `MaskLibraryFraction`

Plus `bBottomLibraryCollapsed` and `bSuppressSplitWriteBack`.

**No ini/GConfig persistence exists for any of these.** No layout store of any
kind was found.

### Collapse pattern (the gallery, proven)

- `bBottomLibraryCollapsed` + `ToggleBottomLibraryCollapsed` — `SMixtormat_Shell.cpp` L568–572.
- Splitter slot `Value_Lambda` returns `0.99 / 0.01` when collapsed (not 1.0/0.0) — L367–404.
- Panel wrapped in `SBox.Visibility_Lambda` → `Collapsed` — L409–417.
- Write-back guard in `OnSlotResized` (`!bBottomLibraryCollapsed`) so collapse
  does not overwrite the remembered fraction.
- Overlay chevron button, style `Mixtormat.BottomLibraryCollapseButton` — L418–450.
- Hotkey `G` in `SMixtormat::OnKeyDown` — `SMixtormat.cpp` L565.
- No explicit `Invalidate` call: Slate re-arranges in `OnPaint`, so the bool flip
  takes effect on the next paint. The pattern works as-is.

### Theme reconstruction (the big constraint)

`SMixtormat::ApplyPendingTheme` (`Widgets/SMixtormat_Theme.cpp` L163–226),
`EThemeRefreshMode::Reconstruct`:

- `ChildSlot[SNullWidget::NullWidget]` → `BuildWorkspaceUI()` — the **entire**
  layout tree is destroyed and rebuilt.
- Only preserved: scroll offsets and inspector-group expansion, via
  `TransferLayoutState` (tree-order traversal, L30–78).
- `SMixtormat` itself, its data, history, and `PreviewViewports` survive.

Any layout feature must survive this rebuild, or be re-applied inside
`BuildWorkspaceUI`.

### Tab / docking

- The whole workspace is one NomadTab: `FGlobalTabmanager::RegisterNomadTabSpawner`
  (`MixtormatEditorModule.cpp` L25–35), `SpawnMixtormatTab` (L141–151).
- No internal docking exists anywhere. `SDockTab` appears only as the tab role
  and in `SMixtormatPopupAnchor.cpp` L108–115 (popup clamping boundary).

### Keyboard bindings (raw `OnKeyDown`, no command list / input chords)

| Key | Action | Location |
|---|---|---|
| `G` | Toggle bottom galleries | `SMixtormat.cpp` L565 |
| `I` | Region ID preview toggle | `SMixtormat.cpp` L514 |
| `F2` / `F12` | Rename selection | `SMixtormat.cpp` L549 |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo | `SMixtormat.cpp` L555–562 |
| `Backspace` | Reset hovered numeric | `SMixtormat.cpp` L510, `SMixtormat_Preview.cpp` L203 |
| `Shift+V` | Viewport overlay (preview) | `SMixtormat_Preview.cpp` L207 |
| `F` | Ramp editor frame view | `SMixtormatRampEditor.cpp` L382 |
| `Escape` | Cancel entry commit | `MixtormatEntryCommit.cpp` L80 |

Free single keys for new panel toggles: `L`, `P`, `B`, etc.

---

## 2. Audit: gallery as a column vertical split

**Verdict: quick win, one line.**

- `BuildBottomLibrary` orientation is a single change
  (`Orient_Horizontal` → `Orient_Vertical`); both fractions work either way.
- Caveats:
  - MATERIALS header is a bare `STextBlock`; MASKS is a header row + toolbar
    button — asymmetric when stacked.
  - `bBottomLibraryCollapsed` + overlay collapse button assume the current shape.
  - `BuildMaskBar` visibility is gated on `bHasWorkingMaterial`.
- If user-toggleable: needs persistence (none exists) + a settings surface.

| Item | Effort | Block |
|---|---|---|
| Orientation swap | 1 line | header asymmetry |
| User-toggleable orientation | Medium | no persistence store |

---

## 3. Audit: drag / redock panels (columns, inspector, layer stack, viewport)

**Verdict: large; three options, none free.**

- **Option A — UE `FTabManager` nested docking.** Heavy. Panels are
  member-function builders, not spawners; needs a panel registry, and the
  theme Reconstruct path would fight the tab manager's own layout state.
- **Option B — custom docking.** Panel registry (ID → builder) + layout tree
  (splits + panel IDs + fractions) + drag handles/drop zones + persistence.
  Big, but fits reconstruction if the layout is pure data.
- **Option C — middle path.** Visibility toggles, swap left/right, persisted
  fractions. Small, low risk.

Blocks:

- No persistence layer exists (all fractions volatile).
- Reconstruction wipes layout; `TransferLayoutState` only handles scroll +
  expansion.
- `PreviewViewports` is deliberately retained across rebuilds — reparenting
  the viewport is risky.
- `SMixtormatPopupAnchor` clamps popups to the SDockTab rect.
- `LayerStackWidth` / `InspectorWidth` are `SBox` overrides
  (`MixtormatDesignTokens.h` L625–626), not splitter-driven.

| Option | Effort | Main block |
|---|---|---|
| A. FTabManager | Large | reconstruction + spawner refactor |
| B. Custom docking | Large | persistence + viewport reparenting |
| C. Toggles + swap | Small | none |

---

## 4. Audit: new "cell" for global variables / settings

**Verdict: placement is easy; global variables are a real data-model feature.**

### Placement options (any of these is small)

- Third tab in the left panel: `SMixtormatTabStrip` supports N options
  (`UI/Controls/SMixtormatTabStrip.h`); add a `LeftSwitcher` slot +
  `LeftTabIndex` value.
- Floating window: copy `OpenThemePanel` (`SMixtormat_Theme.cpp` L101–132).
- New shell column: medium (new splitter slot + fraction + collapse guard).

### Global variables: no runtime concept exists

Verified: no `GlobalVariable` / `UMixtormatVariable` anywhere in
`MixtormatRuntime`. Closest existing things:

- `FMixtormatFinalSettings` — per-material final settings
  (`MixtormatMaterial.h` L16–26).
- `UMixtormatEditorSettings` — project settings, `Config = EditorPerProjectUserSettings`
  (`MixtormatEditorSettings.h` L58–71).
- The parameter binding system (`MixtormatParameterBinding.h`):
  `FMixtormatParameterAddress`, references/drivers, `TryWriteFloat/Int/Bool/Enum`,
  `ResolveLinkTarget`, cycle detection. **This is the natural integration point.**

### What global variables need

1. Runtime: new struct (Id, Name, type, value) + array on `UMixtormatMaterial`;
   stable IDs via `EnsureStableIds`; enum values append-only (serialized by value).
2. Binding: extend the address owner scope — `EMixtormatParameterOwnerType` is
   serialized by value, so append only. Extend `GetOwnerStructs`,
   `TryResolve*`, `ApplyDirectReferences`, cycle checks.
3. Gather: references resolve before gather, so the GPU side likely needs no
   change if globals resolve through the same path.
4. Editor: new panel (inspector-style cards), undo integration, save/load,
   clipboard.
5. **Undo hazard:** `FEditHistoryState` (`SMixtormat.h` L157–165) snapshots only
   Layers, Groups, `bRotateUV90`, `FinalSettings`. A variable table must be
   added there or undo corrupts it.

| Piece | Effort | Block |
|---|---|---|
| Placement (tab / window) | Small | none |
| Global variables data model | Large | runtime + address namespace + history |

---

## 5. Audit: collapse Layers / Inspector (buttons + hotkeys)

**Verdict: small, no architectural change. Copy the gallery pattern.**

### What's needed

1. Two bools on `SMixtormat` (survive theme reconstruction — state lives on the
   widget, not the tree).
2. **Gotcha:** left/right slots have **no write-back guard**
   (`ShellLeftFraction` / `ShellRightFraction` written unconditionally,
   `SMixtormat_Shell.cpp` L352–357 and L453–458). Collapsing without adding
   `!bLeftPanelCollapsed` / `!bInspectorCollapsed` guards will clobber the
   remembered split.
3. Value lambdas (`0.99/0.01`) + visibility wrap on `BuildLeftPanel()` /
   `BuildInspectorPanel()`.
4. Buttons: overlay chevrons (gallery precedent) or top-bar actions.
   **No `ChevronLeft` icon exists** — only `ChevronUp` / `ChevronDown` /
   `ChevronRight` (`MixtormatIcons.h`). Add one or reuse the gallery's
   Right/Down semantics.
5. Hotkeys: add to `OnKeyDown`. `L` (layers) and `P` (inspector) are free.

### Caveats

- Left column = LAYERS + LIBRARY tabs together; collapsing hides both.
- Inspector has no header — chevron placement needs a decision (gallery-style
  overlay is the closest precedent).
- Collapse state is volatile. Persisting needs `UMixtormatEditorSettings`
  (additive config fields). Do **not** put it in the theme —
  `FMixtormatShellMetrics` explicitly excludes layout from themes
  (`MixtormatTheme.h` L736+).
- Tooltips should advertise the key, matching the `(G)` convention.
- Reuse or add tokens for button size — no local styling.

| Piece | Files | Size |
|---|---|---|
| Buttons | `SMixtormat_Shell.cpp`, `SMixtormat.h` | Small |
| Hotkeys | `SMixtormat.cpp` | Trivial |
| Persistence (optional) | `MixtormatEditorSettings.h` | Small |

---

## 6. Audit: inspector as viewport overlay (dock / collapse / resize)

**Verdict: feasible; all four asks are one feature — a placement model for the
inspector.**

### Precedents

- Viewport overlays: `BuildPreviewPanel` `SOverlay` slots
  (`SMixtormat_Preview.cpp` L1520–1631).
- Floating window: `OpenThemePanel` (`SMixtormat_Theme.cpp` L101–132).
- Collapse: gallery pattern (§5).
- Resize: `SSplitter` everywhere, but **no generic resizer widget** — an
  overlay needs a custom drag handle (an empty splitter slot would steal
  viewport mouse input).

### Three placements

| Option | How | Trade-off |
|---|---|---|
| A. Second instance | Build `BuildInspectorPanel()` again in an overlay slot | Trivial, but duplicates a huge tree; two scroll/expansion states diverge |
| B. Reparent one instance | Store `TSharedPtr<SWidget>` member; `BuildWorkspaceUI` places it per mode | One instance, one state; matches the reconstruction idiom — **recommended** |
| C. Separate window | Copy theme-panel pattern | Simplest, but not an in-viewport overlay |

### What's needed (option B)

1. `bInspectorOverlay` bool + `InspectorOverlayFraction` on `SMixtormat`.
2. `BuildWorkspaceUI` decides placement: right splitter slot vs new overlay
   slot in `BuildPreviewPanel`.
3. Toggle button (inspector has no header — viewport overlay cluster or top
   bar) + hotkey (`P` is free).
4. Resize: thin drag handle on the overlay's left edge (mouse capture + delta
   → fraction), clamped.
5. Collapse: §5 pattern for docked mode; overlay mode collapses via the same bool.

### Blocks / risks

- **Theme Reconstruct wipes the tree** — placement must be re-applied in
  `BuildWorkspaceUI`; `TransferLayoutState` is tree-order based, so
  scroll/expansion transfer is per-mode (works, but indices change between modes).
- **Overlay collision** — right-center `GeometryControls` and top-right light
  gizmo sit where the inspector overlay lands. Decide: shift, gate, or cover.
- **`bPreviewOverlayUiVisible` (H)** — should H hide the inspector overlay too?
  Design decision; recommend it's a panel, not a HUD cluster.
- **Width** — `MixtormatTokens::InspectorWidth` is fixed; overlay needs
  fraction-based width (new state + clamp).
- Hit-testing is fine (overlay only blocks where it is); `SMixtormatPopupAnchor`
  clamps to the tab rect, unaffected.

| Piece | Size |
|---|---|
| Overlay slot + toggle + hotkey | Small–medium |
| Reparent single instance | Medium |
| Resize handle | Small |
| Docked collapse | Small |

---

## 7. Summary

### Quick wins (small, low risk)

1. Gallery orientation swap — 1 line.
2. Left-panel tab or floating-window cell — follows existing patterns.
3. Collapse Layers / Inspector buttons + hotkeys — copy gallery pattern;
   add the missing write-back guards.
4. Inspector overlay + toggle + hotkey (option B).

### Real work (medium–large)

1. Layout persistence — no store exists; `UMixtormatEditorSettings` is the
   natural home (additive config).
2. Panel redocking — reconstruction + viewport reparenting are the hard parts.
3. Global variables — runtime model + address namespace extension + undo
   history integration.

### Suggested order

1. Collapse toggles + hotkeys (smallest, immediate value).
2. Inspector overlay + toggle (placement model in `BuildWorkspaceUI`).
3. Overlay resize handle.
4. Layout persistence in editor settings.
5. Global variables (data model first, panel second).
6. Full redocking only if still wanted after the above.

---

## 8. Audit: overlay-only left navigation and galleries

**Decision (D30–D32, user-confirmed):** there are no left-navigation or gallery
splitter cells. The rail is glued/pinned over the viewport edge and reserves no
shell width. LAYERS, LIBRARY, and GLOBAL open in one shared left overlay surface;
Layers alone supports dragging away and returning to the rail. The gallery is one
bottom overlay drawer with a MATERIALS/MASKS mode switch.

### Verified current implementation

- `BuildAuthoringPage` still allocates a three-slot horizontal splitter
  (left / preview+gallery / inspector) and a vertical preview/gallery splitter.
- `BuildLeftColumn` allocates a fixed-width rail plus `LeftSwitcher` cell.
  The rail is docked; only the Layers widget can move to the preview overlay.
  LIBRARY and GLOBAL remain in the cell.
- `BuildBottomLibrary` contains its own horizontal MATERIALS/MASKS splitter.
- The current overlay controller already supports one reparented widget,
  auto-fit height, drag, corner/side resize, viewport clamping, and fronting.
  It currently models only Inspector and the Layers stack.

### Options

| Option | Design | Trade-off |
|---|---|---|
| A. Reuse one left overlay host | Keep the rail as a narrow, fixed overlay at the viewport edge; clicking LIBRARY/GLOBAL opens their content in the same overlay surface. Layers uses that surface and retains its own drag/resize/return affordance. No left shell splitter slot or page cell. | Minimal shell width; requires page switching/reparenting and preserving each page's scroll state. |
| B. Independent overlay per page | Give Layers, Library, and Global separate overlay frames. | Fast page switching, but more geometry/front-order state and more overlap; avoid unless simultaneous panels are required. |
| C. External window / nested docking | Separate OS windows or `FTabManager`. | Does not meet the in-viewport overlay goal; adds lifecycle/reconstruction complexity. |

**Decision:** Option A. Pin the rail to the viewport edge as an overlay control;
it is not draggable and has no shell slot. Clicking an icon opens that page in the
shared overlay surface; choosing another page replaces its content. Layers alone
can be dragged out. Clicking LAYERS while it is out returns/snaps it to its home
position beside the rail; dragging it onto the rail is the alternate return path.
Library and Global do not get separate pop-out geometry. Reparent the existing
widget instances; never create duplicate Layers, Library, or Global trees.

### Gallery overlay options — decision: one bottom drawer

| Option | Design | Trade-off |
|---|---|---|
| A. Single bottom drawer | Remove the center vertical splitter; anchor one materials/masks drawer over the viewport bottom. Replace the internal split with MATERIALS / MASKS tabs or a compact mode switch. | Removes both divider handles and preserves screen space when closed; changing modes is one extra action. **User-confirmed.** |
| B. Two independently open drawers | Keep materials and masks in separate overlay panels. | More simultaneous visibility, but adds stacking, placement, and width state; unnecessary unless side-by-side viewing is a requirement. |
| C. Keep current split and paint over it | Merely style the existing split panels as overlays. | Still reserves layout space and retains both splitters; does not satisfy the request. Reject. |

### Agreed interaction model and implementation constraints

- Remove splitter fractions/write-back for the removed left and preview/gallery
  divisions; do not retain hidden splitter slots as a compatibility layer.
- Keep the right Inspector dock/overlay/hidden cycle and its width behavior.
  Recalculate the main authoring layout as Preview + Inspector; the preview
  owns the overlay host for left navigation, Inspector, and gallery.
- Reparent existing panel instances; preserve search, scroll, selection, and
  gallery state across overlay close/open and theme reconstruction.
- Rail is viewport-pinned (“glued” to the edge), visually above the preview,
  outside shell layout sizing. The rail itself never floats or consumes a splitter
  cell. It remains available while content overlays are hidden.
- Rail click opens or switches the single left overlay page. Clicking LAYERS while
  its stack is popped out returns it home beside the rail; dragging the stack onto
  the rail also returns it. Library/Global switch in the same surface and have no
  independent free-floating geometry. No outside-click policy may discard state.
- Gallery mode selection should preserve the existing MATERIALS and MASKS
  builders and their state. Use tabs/segmented mode selection in one bottom drawer;
  opening/closing or switching mode must preserve search, zoom, selection and scroll.
  Verify mask availability when no working material exists; retain the current visibility rule.
- Keep the Inspector's current right-column Docked / Overlay / Hidden cycle. Its
  shell splitter is not part of the removed left/gallery divisions.
- Reuse `MixtormatOverlay` geometry and theme tokens. Do not invent independent
  sizing literals. Overlay overlap order must be deterministic when Inspector,
  left content, and gallery are open together.
- These choices remove volatile fractions, but placement/size persistence remains
  a separate opt-in task; current layout state is session-only.

### Suggested migration / test order

1. Extract the left rail from the left cell and host it over the preview without
   allocating shell width; verify all three page selections.
2. Route Layers, Library, and Global through one overlay surface; test click and
   drag return for Layers and preserve page state on switching.
3. Replace the preview/gallery vertical splitter with a bottom overlay drawer;
   replace the MATERIALS/MASKS split with a mode selector.
4. Remove obsolete fraction fields, splitter callbacks, and collapse wiring only
   after all readers are updated.
5. Test narrow/wide windows, resize/reconstruction, hit testing, overlay order,
   text entry, mask visibility, and Inspector coexistence.
