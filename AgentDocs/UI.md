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
  `SMixtormatLayerGroupContainer.*`, `SMixtormatLayerConnector.*`,
  `SMixtormatLayerIcon.*`, `SMixtormatLayerSurface.*`, `MixtormatLayerBadges.*`.
- Behaviour: `Widgets/Layers/MixtormatLayerActions.cpp`,
  `MixtormatLayerChildren.cpp`, `MixtormatLayerDragDrop.cpp`,
  `MixtormatLayerMenus.cpp`, `MixtormatLayerHierarchy.cpp`,
  `MixtormatLayerClipboard.cpp`, `MixtormatLayersPrivate.h`.

Structural connection menus/labels and the atomic setter are shared by explicit child
address in `Widgets/Layers/MixtormatStructuralConnections.cpp` (interaction v1 D3).
Inspector Push/Warp wrappers use this adapter; existing sliders remain in place. D1 adds
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
plus `MixtormatThemeSchema.cpp`. Widgets read them through
`FMixtormatThemeStore::GetResolved()` and never inline a value. Known local-literal
gaps and the new-UI checklist: `auditdocs/ui-style-token-audit.md`.

## Viewport overlays / toolbars

`Widgets/SMixtormat_Preview.cpp::BuildPreviewPanel` builds overlay controls and
assembles the preview UI; shared preview state/setters are on `SMixtormat`.
`Widgets/SMixtormatPreviewViewport.*` owns viewport rendering/scene and input,
delegating workspace actions back to `SMixtormat`.
Scene/lighting constants: `Preview/MixtormatPreviewSceneSettings.*`.
Light gizmo: `Preview/SMixtormatLightGizmo.*`.

Workspace layout: Layers/Library/Global occupy a resizable left column; Layers alone can
pop out and return by rail click or snap-back drag. The gallery is one resizable bottom
MATERIALS/MASKS drawer over the whole workspace, replacing both gallery splitters. The
Inspector remains dockable. See the overlay-workspace handoff for validation.
