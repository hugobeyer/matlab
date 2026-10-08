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
`code_docs/noise_gate_flow_handoff.md`. All six generators now expose the flow-tool menu;
Noise and Cliff use Height steering and explicitly explain why Signed Distance is disabled.

## Generator relationship UX plan

`code_docs/generator_relationship_ux_plan.md` specifies the proposed target-first source picker,
atomic connected creation, target-owned display projection, repair/collapse behavior, and complete
UI STYLE integration. It is a textual implementation handoff, not delivered behavior; no concept
image reads or testing are required or authorized. Existing runtime ownership/order remains intact.

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
plus `MixtormatThemeSchema.cpp`. Every UI STYLE registration needs a current reader;
the schema is the editable/save contract, and loading merges it over non-schema state.
Widgets read them through `FMixtormatThemeStore::GetResolved()` and never inline a
value. Known local-literal gaps and the new-UI checklist: `auditdocs/ui-style-token-audit.md`.

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
See the overlay-workspace handoff for validation.
