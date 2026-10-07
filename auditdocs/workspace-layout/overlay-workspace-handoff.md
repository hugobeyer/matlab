# Overlay workspace implementation — new-chat handoff

Status: **implemented in source; rebuild/in-editor validation pending**. The original
D30–D32 direction was revised during implementation. Read this first, then
`decisions-log.md` I14 and `auditdocs/ui-style-token-audit.md`.
Source remains the authority.

## Current approved layout

- LAYERS / LIBRARY / GLOBAL are a normal, resizable **left shell column**.
- Layers alone can pop out; drag it home-to-overlay, click LAYERS to return it, or
  snap it back to the column. Keep one live Layers widget and its retained state.
- Library and Global stay as left-column pages; neither can float.
- The Inspector keeps its existing right-side Docked / Overlay / Hidden behavior
  and `P` binding.
- The gallery is one resizable **bottom overlay drawer over the entire workspace**:
  it covers the left column, Preview, and Inspector without reserving shell height.
- MATERIALS and MASKS are simultaneous side-by-side columns, not tabs. Preserve
  both builders and existing search/zoom/selection/scroll state.
- Default Preview control rails are removed. `Q` marking menu and GLOBAL controls
  remain. The light gizmo appears during RMB lighting rotation by default and has a
  GLOBAL → LIGHTING visibility toggle.
- Overlay stacking remains deterministic: the full-workspace gallery drawer is above
  shell columns; Inspector/Layers pop-out behavior remains independent.

## Current source (before implementation)

- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Shell.cpp`
  - `BuildAuthoringPage()` currently builds horizontal [Left | Center | Inspector].
  - Center currently builds vertical [Preview | BottomLibrary].
  - `BuildLeftColumn()` builds the icon rail plus `LeftSwitcher` cell.
  - `BuildFloatingLayerStack()` wraps Layers for overlay drag/resize grips.
  - `BuildBottomLibrary()` is in `SMixtormat_Library.cpp`; it has a horizontal
    MATERIALS / MASKS splitter.
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Overlays.cpp`
  - Existing placement controls, shared overlay host/fronting, Fit-height action,
    and pointer/drag routing for Inspector and Layers.
- `Source/MixtormatEditor/Private/Widgets/SMixtormatOverlayPanel.*`
  - Shared overlay geometry: auto-fit height, clamping, drag/resize grips, hit tests.
  - Do not fork it for gallery. Extend only where semantics actually match.
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Library.cpp`
  - Existing material and user-library builders; reuse these as overlay content.
- `Source/MixtormatEditor/Private/Widgets/Layers/MixtormatLayerChildren.cpp`
  - `BuildMaskBar` / mask gallery builder and its existing visibility rules.
- `Source/MixtormatEditor/Private/Widgets/SMixtormat.h`
  - Retained workspace state and panel hosts. Search every read of split fractions,
    collapse flags, overlay state and widget pointers before removing obsolete members.
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Theme.cpp`
  - Theme reconstruction currently rebuilds the widget tree. Preserve user state on
    retained `SMixtormat` and reparent one panel instance; close transient menus before
    reconstructing.
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatTabStrip.*` remains used in
  UI STYLE. Do not remove it as part of this workspace change.

## Implementation outline

1. Build the main shell as [Preview | Inspector], with the overlay host above the
   Preview. Confirm the Inspector splitter still resizes and its Overlay/Hidden states
   still return its share correctly.
2. Place the rail in that viewport overlay, pinned to its left edge. It must neither
   affect Preview geometry nor intercept pointer input outside its own buttons.
3. Refactor left-page content to one shared overlay frame/host. Reparent the existing
   Layers, Library and Global widget instances as the selected page; preserve page
   state when switching and during theme reconstruction. Layers alone gets the grab,
   free drag/resize and explicit return-to-rail affordance.
4. Convert the gallery to one bottom overlay drawer. Use a single mode switch for the
   existing MATERIALS and MASKS content. Define how the current selected tab, search,
   zoom and mask-without-material state survive switching and closing.
5. Only after all references are migrated, remove obsolete left/gallery splitter
   fractions, callback write-backs, collapse wiring and fields. Keep Inspector split
   state and shell splitter visuals; the Inspector still uses its shell splitter.
6. Update HELPERS, UI/style docs, token schema/defaults and this decision log as the
   exact bindings and UI STYLE controls land.

## UI STYLE panel and tokens — required design work

Rule: do not create duplicate values. Live-retunable defaults belong in
`FMixtormatTheme` + `MixtormatThemeSchema.cpp` + `Config/UIStyleTheme.json`; structural
constraints belong in `MixtormatTokens`. User-dragged geometry stays runtime state.
Expose new visible style controls in existing UI STYLE groups, not a new styling system:

| UI STYLE group | Candidate metric | Notes |
|---|---|---|
| Preview → Layout | `PreviewLayout.LeftRailInset` | Live distance from viewport edge; migrate the current `LeftRailPadding` token if this is its same semantic value. |
| Preview → Layout | `PreviewLayout.LeftRailButtonGap` | Live vertical gap; migrate current `LeftRailButtonGap`, do not retain two sources. |
| Preview → Layout | `PreviewLayout.LeftOverlayGap` | Gap between pinned rail and overlay content. |
| Preview → Layout | `PreviewLayout.LeftOverlayWidth` | First-use width only, initially preserving current Layers width; once dragged, use retained workspace geometry. |
| Preview → Layout | `PreviewLayout.LeftOverlaySurfaceOpacity` | Translucent left surface; retain square/borderless treatment (D16). |
| Preview → Layout | `PreviewLayout.QuickControlsCentreGap`, `QuickControlsRowGap` | Live spacing for the expanded four-card Q marking menu. |
| Preview → Layout | `PreviewLayout.QuickControlsGuideAxisLength`, `GuideAxisThickness`, `GuideAxisOpacity`, `GuideGlowDiameter`, `GuideGlowOpacity` | Live tuning for the faded hairline cross and soft center bloom; use the palette TextMuted role. |
| Gallery → Gallery Layout | `GalleryLayout.DrawerInset` | Drawer offset from the viewport edge; use only if existing gallery inset is not semantically identical. |
| Gallery → Gallery Layout | `GalleryLayout.DrawerInitialHeight` | First-use height only; subsequent drag size stays runtime state. |
| Gallery → Gallery Layout | `GalleryLayout.ModeSwitchGap` | Spacing around MATERIALS/MASKS switch; reuse `HeaderGap` if it truly matches. |
| Gallery → Gallery Surface | `Gallery.DrawerSurfaceOpacity` | Only add if existing palette/surface metrics cannot express it. |

The candidate names are a schema plan, **not fields implemented yet**. The recent
marking-menu guide/spacing source values currently live in `MixtormatTokens`; if
exposed live, move them to `FMixtormatPreviewMetrics` and do not keep duplicates.
Confirm semantics and defaults against the built UI before adding fields. Register each
new live metric in the right schema section with range, step, precision, and an
appropriate refresh mode (reconstruct for layout sizes; live only for painter values). Update
`Config/UIStyleTheme.json` defaults and UI STYLE reset/locate behavior as the existing
schema requires. Don't expose mutable runtime width/height as if theme changes should
override a user's drag.

Structural-only `MixtormatTokens` candidates: minimum overlay dimensions, resize hit
targets, rail hit size, snap-to-rail distance, guide/grab dimensions and safe viewport
margins. Theme palette roles should supply surface/text/outline colors. Preserve existing
square borderless overlay styling. Retain shell splitter style tokens for the Inspector.

## Existing relevant decisions and behavior

- `D30`: viewport-edge rail is pinned and does not reserve shell width.
- `D31`: shared left overlay content; Layers only can pop out and return by click or
  drag to rail.
- `D32`: one bottom gallery overlay drawer, MATERIALS/MASKS mode switch, no splitters.
- Existing `L` currently cycles Layers Docked → Overlay → Hidden. Reconcile it with the
  new “home beside pinned rail / popped out / hidden” model and update its label/help;
  do not silently leave the old three-state cycle with misleading wording.
- Existing `P` cycles Inspector Docked → Overlay → Hidden.
- Preserve F2/F12, undo/redo, slider reset, preview keys, text-entry Escape behavior,
  nested menus, material callbacks, and disabled states.
- Quick controls use `Q`, not Tab. Q toggles closed even when the popup control owns
  focus; Escape/outside click also dismiss. Selecting a mesh or lighting preset closes
  it. Popup controls preserve normal focus/capture.

## Current validation / worktree status

- The user-reported `SMixtormatIconRail.cpp` bracket error was edited, but the user has
  not yet reported a successful rebuild after that correction. Rebuild before starting
  the overlay refactor; the previous compiler error was at the rail `AddSlot` closure.
- Quick-controls spacing and the behind-card faded cross / center bloom were recently
  edited and are unbuilt/untested. Files: `SMixtormat_PreviewControls.cpp`,
  `MixtormatDesignTokens.h`; decision log I13 mentions the guide.
- Q-close and close-on-mesh/light-selection behavior are implemented in source but need
  build and in-editor verification.
- No workspace-overlay implementation for D30–D32 has been done yet; docs only record
  the approved design. Do not report it as implemented.
- No build, terminal, git, or editor command was run for this handoff/documentation task.

## In-editor test checklist after implementation

- Rail remains glued to the viewport edge and consumes no width at varied window sizes.
- Layers/Library/Global open, switch, close, and retain their state; no duplicate widgets.
- Drag Layers out; click LAYERS to return; drag it onto rail to return; test snap threshold.
- Gallery opens/closes over Preview; MATERIALS/MASKS switch correctly and preserve state.
- No left, preview/gallery, or materials/masks splitter remains; Inspector splitter remains.
- Inspector + left overlay + gallery overlap/fronting and hit testing are deterministic.
- Text editing, nested menus, sliders, reset gestures and viewport camera input remain intact.
- Theme reconstruction retains selected page, scroll/search/selection and panel placement/size.
- UI STYLE values update correctly, use existing defaults, and do not overwrite dragged sizes.

## Copy-ready prompt for the next chat

> Read `Mixtormat/AGENTS.md`, then `Mixtormat/auditdocs/workspace-layout/overlay-workspace-handoff.md`.
> Implement the approved D30–D32 overlay workspace: pinned viewport-edge rail with no
> shell width, one shared overlay page for Layers/Library/Global, Layers-only drag-out
> and click/drag return, and one bottom MATERIALS/MASKS overlay drawer. Remove the left
> and gallery splitter cells only after migrating every reader; preserve the dockable
> Inspector and existing panel state/callbacks. Add the UI STYLE metrics described in the
> handoff to the existing Preview/Gallery groups and their theme schema/defaults, keeping
> structural constraints in `MixtormatTokens`. First check/rebuild the recent rail and
> quick-controls edits; do not assume they compile. Implement in small stages and report
> each stage for user in-editor testing. Do not run build/terminal/git/editor commands
> without explicit authorization.
