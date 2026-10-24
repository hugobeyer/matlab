# UI

All Slate lives in `Source/MixtormatEditor/Private`. The main widget is
`SMixtormat` (`Widgets/SMixtormat.h`), decomposed into `SMixtormat_*.cpp`
translation units — search the member function, not the header.

Targeted source audit (2026-10-24): workspace, Library/Gallery/Recipes and parameter
menus/group controls only. Moss and GPU internals were excluded; other sections were
not revalidated. No build, tests or live UI validation; packaging remains unverified.

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
| Welcome & Guide | `Widgets/SMixtormatWelcomeCard.*` |

## Workspace splitters and creation actions

- The left/preview and preview/Inspector splitters use transparent, top-layer resize grips centered on the visual divider. The authored Shell splitter hit-width controls the actual grip width; layer-row input must not shadow it.
- The rail fade and its enclosing box are `HitTestInvisible`; it is decoration only. Each top-layer shell resize-grip positioning wrapper must be `SelfHitTestInvisible` so its wide positioning padding does not intercept the viewport, gallery, or buttons. Only the actual grip leaf is interactive.
- The Layers creation toolbar keeps Layer, Group and Fill Layer labels only while each third of the column fits the complete label. Labels clip within their slots; narrower columns keep centered icons and tooltips.
- The navigation rail's shadow is an X-axis black-opacity ramp rendered over the page, including rows; the rail itself paints above the shadow. Its strength follows the existing navigation-rail fade and shadow opacity settings.

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

Hierarchy diagnostic indicators use `SMixtormatLayerSurface`'s independent right-edge marker,
not the selected-row tint/top hairline or instance-source left marker/glow. Layer, group and
child builders bind `bDiagnosticActive` / `bContainsDiagnostic` dynamically to
`SMixtormat::IsDiagnosticRowActive(Address, bIncludeDescendants)` using stable owner/child GUIDs.
Exact sources use the stronger marker; ancestors (including scoped generator rows) use the
softer marker, so collapsed branches still identify where a diagnostic lives. Owner-only
layer/group addresses have an invalid `ChildId`. Diagnostic state stays owned by `SMixtormat`;
the hierarchy does not infer it from selection or keep a second preview target.
UI STYLE > Layers > Diagnostic owns `Layer.DiagnosticAccent`, `DiagnosticWidth`,
`DiagnosticOpacity` and `DiagnosticAncestorOpacity`; the surface reads them live on paint.

Published child outputs have one `Outputs` context submenu, populated from
`GetCopyableOutputs(GetChildCapabilities(...))` with semantic labels and output-kind icons.
The formerly duplicated flattened Copy rows were removed; children without copyable outputs
omit the submenu. `Widgets/Layers/MixtormatLayerMenus.cpp` owns this menu construction.

Published gate masks keep their output-reference source in the inspector. The Source
card shows a read-only gate source label and hides Texture/Noise/Layer Values selection;
context Source switching is reserved for unlinked masks, preventing loss of the
original named output. Standalone masks keep the normal source selector.

Surfacing children, including Moss, exclusively own the child inspector while selected;
the parent layer's Fill, Channel Influence and Composition sections remain available only
on layer selection. Moss is listed under Surfacing and has a bound-aware COLOR RAMP card.

Ramp preset menus offer **Overwrite Preset…** for saved user ramps, including Skin's
albedo ramp. Overwrites preserve the preset ID and write without deleting the old file
first. Saving a compatible module preset with an existing user name replaces that preset;
module **Overwrite Preset…** also supports user-library overrides of built-in looks.
Artist-facing ramp, module and recipe menus do not expose JSON import/export. Developer
serialization/import/export APIs remain in the preset store.

Scalar curve toolbars now share interpolation, frame, reset, **horizontal Flip** and
an inline Invert toggle. Flip reflects authored point X positions across the curve's
own input domain (including signed Height Remap), while Invert uses the existing
mask shaping / signed-height invert property; color ramps retain their toolbar.

### Layer-output Blur footer

Root Layer Blur is displayed below the layer card as `Blur · Layer Output`, aligned
with the layer rather than indented as a module. It has no child-tree connector or
module drop wrapper and remains visible when the layer's ordinary children collapse.
Multiple root Blurs retain their authored order in the footer. Selection, enable,
Inspector, copy/instance and removal reuse the canonical child row and identity.
Footer rows do not start drag operations; scoped Mask/Curvature Blur rows retain
their existing hierarchy and interactions. This is presentation only: layer-owned
Blur still executes at the end of its own layer rail. The previously identified
Layer GPU effect-weighting integration remains unfinished; this footer change does
not resolve it. No build or runtime validation is claimed for this UI change.

## Context menu convention

Every right-click menu in the stack reads in one order, so the same muscle memory works on a
layer, a group, a child of any type, and a Sources-shelf row:

1. **Create** — applicable child creation (Effects, IDs, Filter, Masks, Generators, scoped
   children).
2. **Type-specific** — only this row's own actions: Outputs, mask Source, Noise Gate,
   Blend Mode, Gating Mask, Replace with…, Colour, Enable/Disable.
3. **Shared verbs** — `Duplicate`, `Copy`, `Copy as Instance`, `Paste`, `Group`/`Ungroup`,
   `Rename`. `Rename` carries the `F2` shortcut wherever it appears.
4. **`Remove`** — always last, always `Trash()`, always `.Destructive()`.

Verbs are unified across layers, generators, behaviors, masks, filters, effects and sources:
the removal entry is **`Remove`** everywhere (not `Delete`, and not `Remove Generator` /
`Remove Behavior` / `Remove Effect` / `Remove Mask` / `Remove Filter` / `Remove Generated Mask`
— the row glyph and name already say what the row is), grouping is **`Group`** / **`Ungroup`**,
and `Copy` / `Copy as Instance` are worded identically on shelf sources and stack children.
`Copy as Instance` is offered only where `FMixtormatChildCapabilities::bSupportsInstancing` is
set, so no menu lists an entry the paste path would refuse. Icons come from `MixtormatIcons`
only — `Duplicate` for the copy pair, `Trash` for every removal — with no local sizing or
palette values. Right-clicking one row of a multi-selection keeps the whole selection, so
`Group` and `Remove` act on what was selected rather than on the row under the cursor.
A group has no `Remove`: `Ungroup` is the non-destructive release and is labelled as such.

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
Mask sources and the Noise gate live in `Widgets/Layers/MixtormatMaskSources.cpp`. A Mask child
picks `Texture`, `Layer Values` or the appended inline `Noise` source; a fourth entry,
`Noise Value from…`, wires the mask to a completed earlier layer/group Noise
`Value` by LayerId/ChildId. `Paste Copied Noise Value` remains for
supported copied mask references. Inline selection
clears published GUIDs first, because a published source otherwise wins; `UsesNoise()` mirrors
`UsesLayerValues()` precedence. Source changes preserve blending, shaping, filters and scope, and
instances stay locked (they mirror their source child). Inline Noise controls reuse
the generator's PATTERN/DISTORTION sections through `BuildNoisePatternPlacementControls`,
without a second Placement card or generator Flow-only rows. The Mask inspector
orders SOURCE → PATTERN → DISTORTION → SHAPING → PLACEMENT → BLENDING. Mask
placement applies to Texture, published, and inline Noise sources; Layer Values
have no spatial placement. Shaping uses Normalize, Input Min/Max, Curve, Offset
and Invert; the redundant Balance/Contrast controls were removed.
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

Generator Color Ramp exposes 0..1 stop coordinates and lookup, while its stored stops remain
signed-height coordinates through `GetUnitRamp` / `SetUnitRamp`. This preserves authored stop
placement without migrations. Source/Normalize and Scale/Offset use paired rows; Mirror Repeat
reflects extended coordinates instead of holding the endpoint colors. Normalize uses the shared
measured-range GPU utility before lookup scaling; source height and scoped masks stay unchanged.
Presets map into the consuming editor's domain. Flow remains a typed vector, not an implicit
height/color source.

Height Remap keeps its signed -1..1 scalar curve, with paired Input Min/Max and Offset/Amount.
Its Normalize Input uses shared measured max-absolute normalization, preserving zero and signs;
input-range scaling happens once afterward. Amount blends against untouched running height,
and Amount zero skips the pass. Balance and Contrast are removed from the complete contract.

Generated Mask uses intuitive cards without dropdowns: SURFACE (`LayerMix`), CURVATURE
(paired `CavityWeight`/`ConvexWeight`, multi-scale radius/gain/rings/macro mix, and scalar
`CurvatureCurve`), OCCLUSION (AO) (`AOWeight` paired with `bAOInvert`, radius and contrast),
SLOPE & DIRECTION (`SlopeWeight` paired with `bSlopeInvert`, angle range/softness, `DirectionWeight`
paired with `bDirectionInvert`, azimuth/elevation/focus), HEIGHT & DRAINAGE (`HeightWeight`
paired with `RidgeWeight`, height offset, and scalar `HeightCurve`), the shared Mask Shaping card,
and a BLENDING card (`BlendMode` chip and `Weight`). Details collapse automatically when signals
are inactive. Curvature combines multi-scale normal Sobel and height Laplacian; Occlusion blends
baked AO and height horizon occlusion; Slope and Direction support full 3D normals combined from
normal maps and height gradients. Mixer scale is bounded on the CPU with `max(Convex, Cavity)`.

Blur uses the generic `FMixtormatBlur` payload with Mode (Gaussian / Directional /
Smear), FOOTPRINT (Gaussian only), DIRECTION (Directional / Smear only), and AMOUNT.
Smear additionally offers TRAIL COMBINE: Average (original box-weighted mean),
Min or Max (component-wise extrema over valid, bilinearly interpolated trail
samples including the center). All use the same signed Length, Angle/Flow guidance,
masking, channel weights and Amount; other Blur modes remain unchanged.
Discrete Mode, Direction Source (Angle / Flow), and applicable Generator/Behavior
Height / Flow / Height + Flow targeting use full-width segmented strips, not dropdowns.
Root Layer Blur alone shows per-channel weights; irrelevant sections are collapsed
instead of rendered disabled. Flow guidance keeps an explicit typed source chip;
its menu omits invalid sources and explains when none are available. Root Layer
Blur executes after the layer's producers and can connect to their completed
same-layer Flow outputs regardless of authored row position; scoped Blur may
use earlier producer rows, including its owner's already-published native
Flow or an earlier Behavior Flow, but never a forward source. Existing
Gaussian, Directional, Smear, scoped mask, copy/instance and output behavior remain.

Feather's Width, Iterations and Direction Bias are nested `BehaviorFeatherSettings`
parameters. Their editor ranges, reset defaults and optional clamps are authored under
`Feather` in `Config/MixtormatParameterAuthoring.json`; GPU Eikonal/Flow iteration
execution retains a 512-pass safety maximum independently of editor settings.

## Group controls

- Group selection shows compact parameter wells, not a separate titled card per control.
- Each control has one label inside its well and an unlabeled enable toggle on the right.
- RMB on the control opens Edit Label, Control Settings, reset, connections, order and removal.
- Add Control opens an owned modal dialog, not a transient submenu, using the popover surface.
  Numeric controls expose type, label, default and scrub Min/Max; commit uses Create Control/Apply.
- Connect from the member parameter: RMB → Assign to Group Control → New Control or an
  existing control. The draft starts with the member's local value/type; that value seeds Reset.
  `ShowGroupControlSetup` copies reflected range/snap via `TryResolveUi`, not effective authoring
  or session overrides. Do not assume its scrub range matches the member's current UI range.
- Enum controls originate from a real enum parameter; no arbitrary enum identity is fabricated.
- Scalar/color ramp consumers participate in the same parameter menu. RMB on a ramp point
  still removes that point; RMB on empty ramp space opens the enclosing parameter menu.
- Connection counts/details belong in RMB, not permanent inspector footers. Validation errors
  remain visible. The optional Attach Target picker filters to the control's type.
- UI ownership: `Widgets/Inspector/MixtormatInspectorGroupParameters.cpp`,
  `MixtormatInspectorGroupControlMenus.cpp`, and `UI/Parameters/SMixtormatGroupControlPopover.*`.
- These UI changes are source-reviewed only; no build or live interaction validation was run.

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

`SMixtormatSegmentedControl` is full-width when it represents a grouped exclusive
choice in an inspector card. Do not wrap it in `MixtormatRow::MakeTrailing`, which
adds a left label and constrains the strip to its content width. The card title or
caption names the group; use the direct strip pattern from Mask Source and Layer
Composition. Apply this to Feather, Boolean, Warp, Deform, Field Combine, and
future grouped segmented choices. A trailing row remains appropriate only when a
label genuinely belongs beside a compact, non-grouped control.

`UI/Controls/`: `SMixtormatSlider.*`, `SMixtormatSegmentedControl.*`,
`SMixtormatTabStrip.*` (UI STYLE panel), `SMixtormatIconRail.*` (left column navigation),
`SMixtormatTile.*`, `MixtormatEntryCommit.*`, `SMixtormatGroupAction.h`.

Text entries use `Mixtormat.TextField` with `SMixtormatTextFieldGradient`.
Child inline rename, search widgets and native spin boxes use the derived
`Mixtormat.InlineTextField`, `Mixtormat.TextField.Search` and
`Mixtormat.TextField.Spin` styles. Inline names and slider entries paint the
field gradient only while editing; resting labels and slider scrub controls retain
their existing presentation. Native spin-box selection remains Slate-owned.

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

All Mixtormat help popovers must use `UI/Menus/SMixtormatHelp.*`; for controls requiring direct `IToolTip`, use its `MakeStyledToolTip` adapter. Do not add Unreal/Slate default white tooltips, `.ToolTipText(...)`, `SetToolTipText(...)`, or independently styled `SToolTip`. Existing group actions own their styled help internally. New controls must preserve user input handling and retrieve their help styling from shared tokens and theme store. See `AgentDocs/HELPERS.md`.

Known source exceptions in the audited workspace: `SMixtormat_Shell.cpp` Undo/Redo and
`SMixtormat_Library.cpp`'s gallery header still use `.ToolTipText`. These are gaps, not precedent.

## Theme / style / tokens

- Layout + palette tokens: `Style/MixtormatDesignTokens.h` (`MixtormatTokens`).
- Runtime theme: `Style/MixtormatThemeStore.*`, `MixtormatTheme.*`,
  `MixtormatThemeSchema.*`, `MixtormatResolvedStyle.*`, `MixtormatRecipes.*`,
  `MixtormatTypography.*`, `MixtormatFont.*`, `MixtormatGroupButton.*`,
  `MixtormatMutableStyleSet.h`, `MixtormatStyleLocator.*`, `MixtormatCompositing.h`.
- The CONTEXT tab owns the viewport hint strip's `ContextLayout` tokens
  (`HintKeycapSize`, `HintKeycapPadding`, `HintKeyActionGap`, `HintItemGap`,
  `HintStripOpacity`, `HintStripInset`), schema-registered with the `HintStrip`
  locator target.
- The CREATE tab owns the Layers creation toolbar's `CreateLayout` tokens
  (`CreationButtonIconSize`, `CreationButtonIconGap`). The ownership badge's
  `GalleryLayout.OwnershipBadgeSize` / `OwnershipBadgePadding` live under
  GALLERY / SHELL > Gallery Layout, alongside `DrawerHeaderGripSize` and
  `MaskHeaderNameWidth`.
- CONTROLS > Layout owns the parameter authoring panel widths
  (`ParameterInfoPanelWidth`, `ParameterRangePanelWidth`,
  `ParameterAuthoringPanelWidth`); CONTROLS > Dialogs owns the bake path picker,
  the dialog window sizes, and the welcome card dimensions (`ControlLayout.WelcomeCardWidth`,
  `ControlLayout.WelcomeCardMaxHeight`). The UI STYLE window itself is deliberately
  untokenized.
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
(UI STYLE > Gallery/Shell > Global Page). `BuildGlobalPage` in `SMixtormat_Shell.cpp`
mounts FINAL COMPOSITE and VIEWPORT & LIGHTING foldouts. Lighting already calls
`BuildPreviewLightingControls(EPreviewControlLayout::Inline)`; geometry uses a local
mesh segmented strip plus shared displacement controls. Scene, camera and render
builders are also reused. A blanket claim that inline layout is still unimplemented
is stale; broader compact-layout consistency remains unverified. Do not delete or fork controls.

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
Bare V always cycles PBR material channels, independent of layer/module selection:
Material → Base Color → Normal → Roughness → AO → Metallic → F0 → Height → Fuzz → Material.
It does not cycle module diagnostics or PostFX output modes. Shift+V resets all diagnostics.
V/W/E feedback now uses a top-center, hit-test-invisible label instead of writing preview
messages to the status bar. `SMixtormat_PreviewHints.cpp::BuildPreviewFeedback` reuses
`PreviewLabel` typography, the shared preview plate and existing inset/padding metrics.
One label is replaced/restarted on repeated actions: hold 1.4s, fade 0.5s, then collapse
(`MixtormatTokens::PreviewFeedbackHoldSeconds` / `PreviewFeedbackFadeSeconds`).
W/E identify the selected module/output, report off/unavailable states, and Shift+V
reports the Material reset. H/Space hides the label with other optional overlays;
material-contract warnings remain separate. Source-reviewed; in-editor verification pending.
W/E remain separate PostFX toggles. `DA_DiagnosticMode` now emits only Mask (0,
including scalar coverage) and Flow (1, including warped UVs); 3 is reserved for
future PostFX IDs and 2 is unused. Artist-authored materials must put their Flow/UV
visualization in branch 1, not the former branch 2. R toggles the existing Region IDs
preview (I remains an alias); IDs have not moved to PostFX yet.
E prefers the preview-only `AccumulatedFlow` snapshot at the selected module's
position in its generator's ordered chain: Flow generators expose the accumulated
result, Warp exposes transported Flow, Sculpt/Deform expose consumed Flow and
Feather exposes the filtered result. It never substitutes a later module's result.
Pre-generation Warp exposes its composed `WarpedUV` map instead; its coordinate
format remains distinct from direction vectors despite sharing PostFX mode 1.
Flow-related eyes use the same persistent PostFX owner as E; mask/scalar eyes use
W's path. Explicit individual Flow/UV outputs remain available in the output menu.
IDs/SDF retain their legacy rendering, and ID primary eyes remain unchanged.
Entering a material-channel view clears active module and PostFX diagnostics so
they cannot tint or replace the displayed PBR channel.
The contextual hint strip (`Widgets/SMixtormat_PreviewHints.cpp::BuildPreviewHintStrip`)
is one compact line of key/action pairs at the viewport's bottom-left: first matching
context wins (quick controls > channel view > debug view > selected module > default
camera keys), at most five pairs on screen. Every pair names a handler that exists in
the viewport client or workspace key handling; nothing aspirational. The strip is
hit-test-invisible, follows the H/Space master flag, and collapses when nothing applies.
Its size, opacity, paddings and inset are the `ContextLayout` tokens, edited under the
UI STYLE > CONTEXT tab ("Hint Strip" section) with the `HintStrip` locator target.
Scene/lighting constants: `Preview/MixtormatPreviewSceneSettings.*`.
Light gizmo: `Preview/SMixtormatLightGizmo.*`.

Workspace layout: Layers/Library/Global occupy a single resizable left column.
Layers no longer has a draggable header or pop-out mode; it is always docked under
the navigation rail. The L shortcut switches between Layers and the previously
selected Library/Global page. The Inspector alone retains Docked / Overlay / Hidden.
The gallery is one resizable bottom
drawer over the whole workspace; its internal three-column `SSplitter` remains. Its header reads `GALLERY`;
`MATERIALS`, `MASKS` and `RECIPES` label its three resizable columns. A header click collapses it; dragging past the normal
Slate drag threshold resizes it. Collapse leaves a fixed-width centred restore tab
(`SMixtormatGalleryTab`, `GalleryTabWidth`) above the status bar: the foldout header surface
with only its top corners rounded (`MakeGalleryTabRecipe` via the recipe's optional per-corner
`CornerRadii` override), a disclosure chevron beside the Library icon.
Column sizes use persisted `GalleryColumnWeights.X/Y/Z` for Materials/Masks/Recipes.
Tile selection borders paint above
thumbnails; the Masks header also shows the selected mask name without applying it.
Drawer side margins, header/collapsed heights and surface opacity live in `GalleryLayout`.
The drawer backing uses `DrawerSurfaceOpacity`; not every child surface is transparent:
recipe cards explicitly use `MakeGroundRecipe()` in `SMixtormat_Recipes.cpp`.
The category popup populates its family list on opening; `All` clears the category filter.
Inspector remains dockable.

### Layer/group recipes — Gallery column

`SMixtormat_Library.cpp::BuildBottomLibrary` mounts `BuildRecipeGallery()` in the
third column. `Widgets/SMixtormat_Recipes.cpp` owns its search, category filter,
recipe cards and shipped/user labels. Recipes capture a group and its layers;
insertion creates an independent editable group, not a live composition reference.

`Widgets/SMixtormat_Presets.cpp` owns insertion and menus: the RECIPES header offers
Refresh Recipes only; card RMB offers Insert Editable Group, Save User Copy,
Update from Selected Group and referenced-asset details. Card click also inserts.
Insertion appends and selects a new group, requires an open/created material and
leaves document/global settings unchanged. Assets are referenced, not embedded.

Create Recipe saves to the project by default and offers **Publish Plugin Recipe…**
only when `Mixtormat.Developer.ParameterMeta` is enabled (otherwise disabled).
Updates capture the selected group and use the shared confirmation dialog, naming
both source group and destination file. They preserve recipe name/category/description,
ID and filename; a missing, changed-identity or ambiguous target is rejected, not created.
Plugin updates require the same developer gate; ordinary users can save separate copies.

`Services/MixtormatRecipeService.*` owns capture, strict dependency-closure validation
and insertion preparation. `Services/MixtormatPresetStoreDocuments.cpp` owns persistence:
- Developer recipes: `<Plugin>/Resources/Presets/Recipes/*.json`; ship all authored recipes.
- User recipes: `<Project>/Content/Mixtormat/Presets/Recipes/*.json`.
- These are JSON documents, not Content Browser `.uasset` entries.
- Ramp/module presets retain their existing Saved-based storage.
- Earlier `Saved/Mixtormat/Presets/Recipes` files are untouched and no longer listed;
  explicitly relocate wanted recipe JSON to the project recipe folder. No automatic migration.

Plugin publishing checks direct asset paths and resolved targets against `/Mixtormat/`,
`/Engine/` and `/Script/`, and rejects project/other-plugin references. It does not check
transitive package dependencies or prove packaged availability. Ship every recipe's full
asset dependency closure and verify it in a clean project. Source-only change; no runtime test.

Artist menus do not expose JSON import/export; developer serialization APIs remain.
Help copy corrected 2026-10-24: recipe cards name Save User Copy, the empty state points
to filters/Create Recipe, and gallery restore help includes Recipes. The welcome guide
names Q for geometry and V/W/E for previews; reset help names Shift+V.
Source-reviewed only; interaction and asset/package availability remain unverified.
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
