# UI

All Slate lives in `Source/MixtormatEditor/Private`. The main widget is
`SMixtormat` (`Widgets/SMixtormat.h`), decomposed into `SMixtormat_*.cpp`
translation units — search the member function, not the header.

## Slate regions

| Region | Files |
|---|---|
| Shell / top bar / splitters | `Widgets/SMixtormat_Shell.cpp`, `UI/Controls/MixtormatShellSplitterStyle.h` |
| Document / tabs | `Widgets/SMixtormat_Document.cpp` |
| Layer hierarchy | `Widgets/SMixtormat_Layers.cpp`, `Widgets/Layers/*`, `UI/Layers/*` |
| Inspector | `Widgets/SMixtormat_Inspector.cpp`, `Widgets/Inspector/*` |
| Library / gallery | `Widgets/SMixtormat_Library.cpp`, `Widgets/Gallery/*` |
| Parameters | `Widgets/SMixtormat_Parameters.cpp`, `UI/Parameters/*` |
| Preview | `Widgets/SMixtormat_Preview.cpp`, `Widgets/SMixtormatPreviewViewport.*`, `Preview/*` |
| Theme panel | `Widgets/SMixtormat_Theme.cpp`, `Widgets/SMixtormatThemePanel.*` |
| Dialogs | `Widgets/Dialogs/SMixtormat{Action,BakeSettings,BakeResult}Dialog.*` |

## Layer hierarchy

- `UI/Layers/SMixtormatLayerHierarchy.*` — the tree.
- `UI/Layers/SMixtormatLayerRow.*`, `SMixtormatLayerGroupRow.*`,
  `SMixtormatLayerChildRow.*`, `SMixtormatLayerContainer.*`,
  `SMixtormatLayerGroupContainer.*`,
  `SMixtormatLayerIcon.*`, `SMixtormatLayerSurface.*`, `MixtormatLayerBadges.*`.
  (`SMixtormatLayerConnector.*` was removed with the brush-based tree — see `ICONS.md`;
  hierarchy rails are painted by `SMixtormatLayerHierarchy` from theme metrics now.)
- Behaviour: `Widgets/Layers/MixtormatLayerActions.cpp`,
  `MixtormatLayerChildren.cpp`, `MixtormatLayerDragDrop.cpp`,
  `MixtormatLayerMenus.cpp`, `MixtormatLayerHierarchy.cpp`,
  `MixtormatLayerClipboard.cpp`, `MixtormatMaskSources.cpp`, `MixtormatLayersPrivate.h`.

Published child outputs have one `Outputs` context submenu, populated from
`GetCopyableOutputs(GetChildCapabilities(...))` with semantic labels and output-kind icons.
The formerly duplicated flattened Copy rows were removed; children without copyable outputs
omit the submenu. `Widgets/Layers/MixtormatLayerMenus.cpp` owns this menu construction.

Structural connection menus/labels and the atomic setter are shared by explicit child
address in `Widgets/Layers/MixtormatStructuralConnections.cpp` (interaction v1 D3).
Inspector Push/Warp wrappers use this adapter; existing sliders remain in place. Connection
menus group sources by layer, use child names without repeated origin paths, abbreviate Warp
outputs to Flow/UV, and show compact unavailable reasons. Full connection-status descriptions
remain available through the existing connection labels. D1 adds
optional Source → Target badges only to Height Push/Structural Warp child rows, using the
same `SMixtormatBadge` dropdown widget as layer blend modes, with bounded label widths and
full-label tooltips. The reusable row keeps other child layouts unchanged. D2 adds structural source/target highlight roles,
active valid incoming counts, and a transient marker for collapsed source layers without
replacing instance-source glow. E1 adds explicit target-row actions in
`MixtormatLayerMenus.cpp`; `CreateStructuralModuleForTarget` validates and inserts a new
unscoped module before that target, connects only its target GUID, and leaves its source unset.
Existing layer-level creation stays unchanged. The shared procedural removal handler accepts
Push, Warp, Height Blend, Height Remap and Height Color Ramp and removes their owned subtree.
The Sources shelf is a non-compositing array card above the layer creation toolbar, built by
`SMixtormat::BuildSourcesShelf` with `UI/Layers/SMixtormatSourcesShelf.*` shell and
`UI/Layers/SMixtormatSourceRow.*` rows. It reuses the inspector foldout's header anatomy,
tokens and surface; the body now uses the shared Card recipe, compact Menu-row styling,
and an always-visible (when expanded) + tab attached at the card's bottom-right edge.
The tab reuses the shared card surface recipe and opens the existing six-generator menu. Sources has its own UI STYLE tab, with independent card spacing, row dimensions, add-button background width/height, square icon size, lower-corner radius and accent strength. The add glyph is centered and never inherits the background aspect ratio.
Empty cards show no instructional text, but retain the + tab. The layout is authored by
`LayerLayout.SourcesBottomGap`, `SourcesEmptyHeight`, `SourcesRowHeight`, `SourcesRowGap`,
`SourcesAddTabWidth`, and `SourcesAddTabHeight` under UI STYLE > Sources.
Expansion is session UI state (`bSourcesExpanded`) and never reaches the
document or the render. Sources are document data (`FMixtormatSourceEntry` on
`UMixtormatMaterial`, mirrored as `WorkingSources` beside -- never inside -- `WorkingLayers`), so
the compositor, height references and grouping never see a source as a stack member. Add Source offers the six generator kinds and starts them through the same `ApplyChildCreationDefaults` a
generator child uses. Rows select on left click and delete from their context menu; selection is
exclusive — layer, child and group selection clear the source and vice versa. The Inspector
shows a SOURCE card (name, kind, enabled) above the kind's own generator panel, which resolves
through the shared `GetSelectedGenerator()` accessor. Sources use `EMixtormatChildOwnerType::Source` (Owner = SourceId) for selection and
Inspector routing. The canonical Runtime `FMixtormatBindingScope` now also accepts
Sources: root-generator and owned-child parameter addresses use their real SourceId
and ChildId; Copy/Paste Reference, Follow/Link, linked writes and Go to Source use
that same scope, never synthetic layer IDs. A transient producer copy applies
direct parameter references before GPU gather. Spatial parameter Drivers on the
shelf remain disabled until a supported signal consumer exists.

Sources are document data: new/open/save/save-as, undo and copy/paste preserve
source identities and bindings. Copy Source / Paste Copied Source create fresh
root/owned-child IDs, remap internal structural target GUIDs and shelf output
references, and retain valid external dependencies. Source IDs, root child IDs and
owned-child IDs are repaired against document-wide namespaces on load. The root
is unscoped and owned children are normalized to the root.

Demanded Sources already evaluate and publish signed Height, Flow and UVMap
fields for generator HeightSource/WarpSource through the producer graph, ahead
of the ordinary stack (see `COMPOSITION.md`). `ResolveContainer` remains null
for Source rows: there is no shelf child hierarchy/tool authoring UI, generic
Copy Output/paste-as-instance authoring, or general mask/ID/colour consumption
there yet. Existing layer-only published-output resolvers remain gated; the
future shelf evaluator extensions must use typed owner-kind keys, not fake layers.

Mask sources and the Noise gate live in `Widgets/Layers/MixtormatMaskSources.cpp`. A Mask child
picks `Texture`, `Layer Values` or the appended inline `Noise` source; a fourth entry,
`Noise Value from…`, wires the mask either to a completed earlier layer/group Noise
`Value` or to an enabled Sources-shelf Noise root by SourceId/ChildId. Shelf
references retain their distinct owner kind, survive source duplication, and
are demanded ahead of the layer stack. `Paste Copied Noise Value` remains for
supported copied mask references. Inline selection
clears published GUIDs first, because a published source otherwise wins; `UsesNoise()` mirrors
`UsesLayerValues()` precedence. Source changes preserve blending, shaping, filters and scope, and
instances stay locked (they mirror their source child). The inline Noise controls reuse the
generator inspector's PATTERN/PLACEMENT rows via `BuildNoisePatternPlacementControls`; generator
Height-only settings do not appear because masks read Value coverage. Mask Tiling/UV/Rotation
stay visible for Texture, published and inline-noise sources and stay hidden for Layer Values.
Right-click a scoped-mask owner (generators and effects, including Gravity Flow) → `Noise Gate`
(`CreateNoiseGate`) to author one scoped Mask child with Source=Noise in a single edit. This UI
half is source-reviewed only; the GPU half is `AddNoiseMaskPass`/`AddNoiseCoveragePass` plus the
mask-resolver conversion in `MixtormatGpuMaskPasses.cpp` (see `COMPOSITION.md`).
Delivered files, UI availability findings, and integration rules for later UI agents:
`old_docs/noise_gate_flow_handoff.md` (archived delivery history). All six generators now expose the flow-tool menu;
Noise and Cliff use Height steering and explicitly explain why Signed Distance is disabled.

## Generator relationship UX — compact child-first authoring

Current implementation:
- Generator context menu offers `Add Warp`, and `Add Height Push` only for eligible
  Strata Carver targets. All actions are omitted when the target cannot accept them;
  no disabled menu row with an explanatory suffix is rendered.
- A click creates a projected structural operation child with its target already
  connected and its source unset. The source is authored on the child itself via
  the compact `Source +` chip, using `BuildStructuralConnectionMenu`.
- Source/target edit menus list compatible endpoints only, grouped by layer.
  The selected operation's context menu continues to provide source navigation,
  target editing and disconnection when applicable. The inspector retains its
  existing parameter controls, including instantiated/invalid state behavior.
- The source-search overlay in `SMixtormatStructuralSourcePicker` and
  `BuildStructuralSourcePickerForTarget` is retained in source for compatibility,
  but is no longer linked from the generator RMB creation path. Do not reintroduce
  that redundant UI without explicit approval.
- The projected relation row shows the operation glyph/name and an editable
  source chip, not a duplicate text arrow or an inline repeat of its target.
  The target is indicated by the parent hierarchy. Invalid/repair rows retain
  their endpoint details and structural diagnostics.
- Generator layers and generator children no longer print `GEN` source text;
  their existing glyphs identify the type. Generator target rows use
  `WarpStructural` and `WarpPush` SVG indicators only when stored incoming
  operations exist, in the freed right-hand row space.
- Shared child menus omit invalid Paste and Noise Gate actions; source and
  target menus omit ineligible candidates while retaining diagnostic status
  in the connection model and Inspector. Conditional availability remains
  validated at activation and during actual edits.

The underlying address-based `MixtormatStructuralConnectionModel.*`, atomic
setter, authored child identity, group expansion, projection and history/preview
contracts are unchanged. `CreateConnectedStructuralModuleForTarget` and the
legacy source picker remain available in source. The new card-matched Sources
add tab uses the registered `Icons/add` SVG and `MakeSourcesAddTabRecipe` to
share `MakeCardBodyRecipe` styling; `LayerLayout.SourcesAddTabHighlight` and
`SourcesAddTabHighlightBias` control its accent ramp, without introducing
a new parallel palette.

## Generator Input controls

`BuildGeneratorInputControls()` is still mounted in `SMixtormat_Inspector.cpp`.
Generator `HeightSource` / `WarpSource` are persisted, disabled-by-default inputs and
are distinct from the Height Push / Structural Warp child module sockets. Do not
remove, alias, or migrate these fields until their runtime/gather behavior is
verified and removal is explicitly approved. The visible hierarchy remains the
canonical authoring surface for Push/Warp operations.

## Inspector builders

`Widgets/Inspector/`: `MixtormatInspectorLayer.cpp`, `MixtormatInspectorMasks.cpp`,
`MixtormatInspectorIds.cpp`, `MixtormatInspectorGenerators.cpp`,
`MixtormatInspectorEffects.cpp`, `MixtormatInspectorHeight.cpp`.
Builders are `SMixtormat::Build*Panel` members.

## Cards / rows / atoms

- Rows: `UI/Rows/SMixtormatRow.*` (`MixtormatRow::MakePair`, `AddSliderRow`).
- Containers: `UI/Containers/SMixtormatInspectorCard.*`, `SMixtormatInspectorGroup.*`,
  `SMixtormatInspectorWell.*`, `SMixtormatFoldoutHeader.*`, `SMixtormatMenuPanel.*`,
  `MixtormatGroupCardPainter.*`.
- Atoms: `UI/Atoms/SMixtormat{Badge,Chip,IconButton,StatusDot,Toggle}.*`,
  `MixtormatIcons.*`.
- Primitives: `UI/Primitives/SMixtormat{GradientBox,SurfaceBox,WellBox}.*`,
  `Mixtormat{GradientPainter,SurfacePainter,Well}.*`.

## Shared controls

`UI/Controls/`: `SMixtormatSlider.*`, `SMixtormatSegmentedControl.*`,
`SMixtormatTabStrip.*` (UI STYLE panel), `SMixtormatIconRail.*` (left column navigation),
`SMixtormatTile.*`, `MixtormatEntryCommit.*`, `SMixtormatGroupAction.h`.

## Ramp widgets

`UI/Controls/SMixtormatScalarRamp.*`, `SMixtormatColorRamp.*`,
`SMixtormatRampEditor.*`. Change these first, then their consumers. Backing
types: `Runtime/Public/MixtormatScalarRamp.h`, `MixtormatColorRamp.h`; math in
`MixtormatScalarRampMath.*`, `MixtormatColorRampMath.*`.

The shared editor paints canvas background/shade, grid, major grid and a separate canvas
border from `GetResolved().ControlLayout`. Both scalar and colour ramps use the shared ramp
toolbar. Its icon buttons opt into `ScalarRampButton`: an Accent gradient, preblended against
`Palette.Ground` when `BodyBlend` is non-Normal. This is a Ground-based approximation, not
sampling the containing surface. State precedence is Active → Hover → Rest; Auto Zoom is active.
The plate currently does not apply inherited tint/opacity or a disabled-specific plate state.

## Tooltip styling contract

All Mixtormat help popovers use `UI/Menus/SMixtormatHelp.*`; for controls requiring direct `IToolTip`, use its `MakeStyledToolTip` adapter. Do not add Unreal/Slate default white tooltips, `.ToolTipText(...)`, `SetToolTipText(...)`, or independently styled `SToolTip`. Existing group actions own their styled help internally. New controls must preserve user input handling and retrieve their help styling from shared tokens and theme store. See `AgentDocs/HELPERS.md`.

## Theme / style / tokens

- Layout + palette tokens: `Style/MixtormatDesignTokens.h` (`MixtormatTokens`).
- Runtime theme: `Style/MixtormatThemeStore.*`, `MixtormatTheme.*`,
  `MixtormatThemeSchema.*`, `MixtormatResolvedStyle.*`, `MixtormatRecipes.*`,
  `MixtormatTypography.*`, `MixtormatFont.*`, `MixtormatGroupButton.*`,
  `MixtormatMutableStyleSet.h`, `MixtormatStyleLocator.*`, `MixtormatCompositing.h`.
- Authored theme data: `Config/UIStyleTheme.json`.

Use the token/theme system; do not introduce local styling. `MixtormatStyle`
(`Style/MixtormatStyle.h`) is the legacy style-set entry point.

Two systems, one rule: structural constants go in `MixtormatTokens`
(`Style/MixtormatDesignTokens.h`); anything retunable live goes in `FMixtormatTheme`
plus `MixtormatThemeSchema.cpp`. Every UI STYLE registration needs a current reader;
the schema is the editable/save contract, and loading merges it over non-schema state.
Widgets read them through `FMixtormatThemeStore::GetResolved()` and never inline a
value. Ramp layout includes height, curve/grid/major-grid thickness, point size, toolbar gap,
colour-ramp height, canvas/grid/border opacity and border thickness. Layer layout includes
`LayerIndent` for group members and separate group/child hairline width and opacity.

Themes save recognized schema properties and retain unrecognized properties only inside known
sections of an existing compatible v1 theme file. Unknown roots/tabs, incompatible files, and
unreadable files are not preserved. Missing recognized keys retain the current in-memory theme
(startup seeds defaults first). Known local-literal gaps and the new-UI checklist:
`auditdocs/ui-style-token-audit.md`.

## Left-column visual contract — Layers / Library / Global

The entire left column, Library and navigation rail use the shared darker `Palette.Shell` ground, matching `Mixtormat.Panel`; `Palette.Panel` is reserved for raised/internal controls.
The rail's inactive button body uses the same ground by default, with active/hover
states retaining the shared button recipe; `PreviewLayout.LeftRailButtonSurfaceStrength`
lifts an inactive plate without hardcoding a colour. `LeftRailHoverSurfaceStrength` and `LeftRailSelectedSurfaceStrength` keep active states subtly raised. `LeftRailShadowOpacity`
controls a local vertical shade repeated inside each rail button, `LeftRailShadeBias` its bias, and
`bLeftRailShadeInverted` reverses the direction (on by default). These are
UI STYLE > Preview > Layout controls, not marking-menu properties.

Sources has separate top and bottom layout spacing under UI STYLE > Layers >
Sources: `LayerLayout.SourcesTopGap` measures header inset, while
`SourcesBottomGap` separates it from layer creation actions. Both remain
inside the common left column, whose own `ColumnGutter` is independent.

Global uses existing `SMixtormatInspectorGroup` foldouts and `AddCard` card
recipes, with `Shell.GlobalPagePadding` and `Shell.GlobalCardGap`
(UI STYLE > Gallery/Shell > Global Page). Generator and material behavior
is unchanged. Preview render/lighting/geometry/camera/output widgets are still
shared with the viewport; **their internal layout remains a known follow-up**:
give these builders an explicit compact row-mode variant for Global before
replacing viewport-specific button geometry. Do not delete or fork controls.

Library search is one `SMixtormatWellBox`, including the unplated folder icon,
with `Shell.LibrarySearchInnerPadding`. Content beneath the search is a
thumbnail-bearing list of saved mixes and imported user surfaces, using
`SMixtormatTile` and the shared card-body surface recipe; existing right-click
asset actions remain. Library no longer shows arbitrary editable-layer counts,
because that count is not an entry name or asset type. Layout, item spacing and
thumbnail size (28px default, with a 2px item gap) are governed by `Shell.LibraryPagePadding`,
`LibrarySearchBottomGap`, `LibraryItemGap`, `LibraryRowHeight`, and `LibraryThumbnailSize`. The row is 34px by default and thumbnails are clipped to its inner height even when an older saved theme still stores a larger thumbnail size.
Text follows shared `CardTitle` / `LayerName` typography and
`Palette.TextMuted` / `Palette.Text` with additional
`LibraryHeadingOpacity` and `LibraryLabelOpacity`. All are registered under
UI STYLE > Gallery/Shell > Library Page / Library Typography.

Do not create a second independent label opacity, card recipe, or marking-menu
spacing system for these pages. Preserve all editing, import, context-menu,
navigation, and preview interactions when modifying presentation.

## UI STYLE locator

`Style/MixtormatStyleLocator.*` selects the visible matching widget nearest the UI STYLE panel,
then `SMixtormatLocatorOutline` paints exact bounds in the shell-root `SOverlay`. The marker is
a two-pulse, 1.2-second outline using `Palette.Modified` with 0.12 fill and 0.5 border opacity.
Targets include shell, controls, foldouts, cards, layers, buttons, menus, preview, gallery,
`TopBar`, `NavigationRail`, `SSplitter`, and `SScrollBox`. The theme panel itself is excluded.
The outline paints only in its target window, so popup/menu targets can locate but are skipped
when their target is in another window. Typography `Body` deliberately has no live target.

## Viewport overlays / toolbars

`Widgets/SMixtormat_Preview.cpp::BuildPreviewPanel` builds overlay controls and
assembles the preview UI; shared preview state/setters are on `SMixtormat`.
`Widgets/SMixtormatPreviewViewport.*` owns viewport rendering/scene and input,
delegating workspace actions back to `SMixtormat`.
Scene/lighting constants: `Preview/MixtormatPreviewSceneSettings.*`.
Light gizmo: `Preview/SMixtormatLightGizmo.*`.

Workspace layout: Layers/Library/Global occupy a single resizable left column.
Layers no longer has a draggable header or pop-out mode; it is always docked under
the navigation rail. The L shortcut switches between Layers and the previously
selected Library/Global page. The Inspector alone retains Docked / Overlay / Hidden.
The gallery is one resizable bottom
drawer over the whole workspace, replacing both gallery splitters. Its header reads `GALLERY`;
`MATERIALS` and `MASKS` label its two panes. A header click collapses it; dragging past the normal
Slate drag threshold resizes it. Collapse leaves a fixed-width centred restore tab
(`SMixtormatGalleryTab`, `GalleryTabWidth`) above the status bar: the foldout header surface
with only its top corners rounded (`MakeGalleryTabRecipe` via the recipe's optional per-corner
`CornerRadii` override), a disclosure chevron beside the Library icon.
Fresh layouts allocate 67% to Materials and 33% to Masks. Tile selection borders paint above
thumbnails; the Masks header also shows the selected mask name without applying it.
Drawer side margins, header/collapsed heights and surface opacity live in `GalleryLayout`.
Child gallery backgrounds stay transparent so the drawer opacity can reveal the preview.
The category popup populates its family list on opening; `All` clears the category filter.
Inspector remains dockable.
The left rail has its own `NavigationRail` icon role (18px glyph, 24px shipped target),
independent of toolbar sizing. It overlays the full-width left page's leading
`PreviewLayout.LeftRailContentInset` (28px shipped default); the page surface remains one
continuous column. All three pages are docked, with no extra Layers grab margin. The tab group has no separate spine, no neck fill and no offset drop
shadow: `MakeNavigationRailTabRecipe` reuses the shared group-button recipe
and shades each rail button independently. A separate horizontal fade is painted above the page and beneath the buttons.
`PreviewLayout.LeftRailShadowOpacity` retains its saved ID but now means
vertical shade strength, and `PreviewLayout.LeftRailShadeBias` controls the
vertical distribution (higher means more shading near the bottom).
`LeftRailButtonGap = 0` keeps the tabs adjoining; the existing corner radius
applies only to the outside corners, never internal seams. Border opacity
and thickness control a dedicated four-edge rail hairline.
`LeftRailShadowOffset` and `LeftRailShadowRadius` remain serialized for
existing themes but no longer draw an offset shadow. The retired Layers pop-out
width/opacity fields also remain serialized for theme compatibility, but have
no active UI STYLE controls. The per-button shade and borders stay inside each tab; the separate
horizontal fade extends across the page beneath the rail. The Q marking menu shares the existing 1K/2K/4K composition
resolution control. Its backdrop is a centre-dark, edge-transparent vignette behind the cards;
UI STYLE exposes its diameter and darkness under Preview. The saved `QuickControlsGuideGlow*`
IDs remain unchanged for theme compatibility, but no longer describe a light bloom. Ctrl+wheel changes shared camera FOV within its existing bounds; plain wheel
retains camera zoom.
The validation checklist is retained in the archived `old_docs/overlay-workspace-handoff.md`.
