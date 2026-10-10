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
No build/runtime validation has been run.

The Sources shelf is a non-compositing list below the layer rows, built by
`SMixtormat::BuildSourcesShelf` with `UI/Layers/SMixtormatSourcesShelf.*`. It reuses the
inspector foldout's header anatomy, tokens and surface; expansion is session UI state
(`bSourcesExpanded`) and never reaches the document or the render. The body is an empty state
plus a disabled `Add Source` action -- generator, ramp and float sources are not implemented, so
nothing is offered as a working control. The shelf sits outside the layer scroll box so its
header stays reachable. UI shell only; no build/runtime validation run.

Mask sources and the Noise gate live in `Widgets/Layers/MixtormatMaskSources.cpp`. A Mask child
picks `Texture`, `Layer Values` or the appended inline `Noise` source; a fourth entry,
`Noise Value from…`, wires the mask to a completed earlier Noise generator's live published
`Value` (with `Paste Copied Noise Value` when the clipboard holds such a mask). Inline selection
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

## Generator relationship UX plan

The archived `old_docs/generator_relationship_ux_plan.md` records the delivered primary UI implementation. Generator RMB now offers
`Warp using…` and `Height Push from…` through the searchable, grouped
`UI/Menus/SMixtormatStructuralSourcePicker.*`. Unavailable rows retain canonical reasons and full
tooltips; keyboard Up/Down selects eligible rows, Enter activates, and Escape dismisses without edits.
`MixtormatStructuralConnectionModel.*` owns the effective/resolved context and typed source collector
shared with existing endpoint menus. `PrepareConnectedStructuralModuleForTarget` revalidates at the
real insertion boundary; the connected creator commits source and target in one history/preview edit.
Each source picker now offers `Choose source later`, which creates the operation with its target
set and no source; this replaces the removed `Advanced → Add unconnected…` submenu. Layer-level
creation remains available. Generator add menus now match the creator's layer/unscoped ownership
gates instead of offering clickable no-ops.
`MixtormatStructuralConnectionProjection.*` now generates target-owned display rows for safe local
Warp/Push blocks. Every authored child remains represented once; owned masks/tools stay beneath the
actual operation, while unset/missing/ambiguous target data remains an authored repair row. Incoming
labels retain source breadcrumbs, typed-output/status tooltips and authored execution-position text.
Visible descriptors supply scope paint metadata; relation direction is a local chevron, not a permanent
source-to-target rail. The existing child-row shell gains optional connection content and local halo
suppression; ordinary rows keep their anatomy and instance-source markers. Selection/menu/enable/drag
of relation rows resolves the real address; ambiguous repairs keep their precise authored lane.
Connection RMB exposes Change source/target and Disconnect source without resetting trace controls.
Layers → Connections now owns Indent, Inset, TextGap, PickerWidth and PickerListMaxHeight through the
schema/resolved theme and existing persistence/refresh routes; prior authored theme values stay intact.
Generator-local collapse uses transient address-keyed `CollapsedGeneratorAddresses`; collapsed rows
show stored incoming counts with active-valid/issue details in tooltips. `RevealChildInHierarchy` and
`NavigateToChild` reveal ancestors and scroll uniquely addressed rows; ambiguous identities are rejected.
`BuildStructuralRelationshipHeader()` resolves the live inspector selection and exposes Go to source,
including uniquely mapped shared-group producers. Existing endpoint editors and instance gates remain.
Picker-owned `FMixtormatStructuralEndpointPreview` supplies temporary endpoint highlights through a weak
editor reference, without selection/history/compose edits. Activation, Escape, dismissal, rebuild and
document/history-baseline changes invalidate previews. Collapse is pruned on rebuild and reset for a
new document/history baseline. Generator disclosure hit/glyph dimensions are bounded by ChildRowHeight;
ordinary layer icons retain their default dimensions. No runtime ownership/order migration was made.
The user confirmed compilation through phase 4; this latest collapse/navigation/highlight slice is
source-reviewed only. No agent tests, diagnostics, builds or commands, or runtime/visual/performance
validation, were performed.

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

Workspace layout: Layers/Library/Global occupy a resizable left column; Layers alone can
pop out and return by rail click or snap-back drag. The gallery is one resizable bottom
drawer over the whole workspace, replacing both gallery splitters. Its header reads `GALLERY`;
`MATERIALS` and `MASKS` label its two panes. A header click collapses it; dragging past the normal
Slate drag threshold resizes it. Collapse leaves a thin clickable restore strip with the Library icon.
Fresh layouts allocate 67% to Materials and 33% to Masks. Tile selection borders paint above
thumbnails; the Masks header also shows the selected mask name without applying it.
Drawer side margins, header/collapsed heights and surface opacity live in `GalleryLayout`.
Child gallery backgrounds stay transparent so the drawer opacity can reveal the preview.
The category popup populates its family list on opening; `All` clears the category filter.
Inspector remains dockable. These gallery changes have source review only, not visual validation.
The left rail has its own `NavigationRail` icon role (18px glyph, 30px target by default),
independent of toolbar sizing. The Q marking menu shares the existing 1K/2K/4K composition
resolution control. Its backdrop is a centre-dark, edge-transparent vignette behind the cards;
UI STYLE exposes its diameter and darkness under Preview. The saved `QuickControlsGuideGlow*`
IDs remain unchanged for theme compatibility, but no longer describe a light bloom. Ctrl+wheel changes shared camera FOV within its existing bounds; plain wheel
retains camera zoom. These additions have source review only, not build or runtime validation.
The validation checklist is retained in the archived `old_docs/overlay-workspace-handoff.md`; runtime validation is not implied.
