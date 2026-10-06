I’ll trace the authored schema through actual Slate paint/layout consumers, without edits or commands.

I’ve confirmed the refresh currently rebuilds the workspace on every edit; next I’m tracing the consumers and legacy bypasses.

The core mismatch is now clear: only selected primitives read Stage‑9 state; most production chrome still refreshes legacy styles. I’m mapping the remaining controls, text, icons, and preview construction paths.

## Executive findings

- Stage‑9 has a valid `Theme → Validate → Resolve` pipeline.
- It does **not** have one production reader system.
- Primitive recipes are mostly wired; production containers remain mostly legacy.
- Every edit requests a full workspace rebuild after `0.1s`.
- No paint/layout/reconstruct distinction exists.
- `FMixtormatStyle::Refresh()` rebuilds legacy token/palette styles, not Stage‑9 styles.
- Most typography roles are schema-only; only `GalleryCaption` reads Stage‑9 typography.
- Most icon roles are partial or dead; many sizes still use `MixtormatTokens`.
- `UIStyleTheme.json` is manual-load only; startup loads old `LiveTheme`, not UI STYLE.
- Locator is tab/type guessing, not property/semantic targeting.

## Broken / misleading properties

| Property | Class | Actual reader | Problem | Required fix |
|---|---|---|---|---|
| `Toggle.Size` | D | `SMixtormatToggle.cpp:37` | Uses `MixtormatTokens::ToggleSize` | Use resolved toggle size |
| `Toggle.FillInset` | D | `SMixtormatToggle.cpp:55` | Uses legacy token | Use resolved inset |
| `Toggle.DisabledShade*` | D | `SMixtormatToggle.cpp:44-45` | Uses legacy tokens | Pass resolved values |
| `ControlLayout.*` except disabled opacity | D/F | Rows/controls use tokens | Schema fields do not drive production layout | Migrate or remove |
| `FoldoutLayout.*` | D | `SMixtormatInspectorGroup.cpp`, `SMixtormatFoldoutHeader.cpp` | All geometry uses tokens | Migrate all geometry |
| `CardLayout.*` | D | `SMixtormatInspectorCard.cpp` | Padding/margins use `MixtormatTokens` | Migrate or remove |
| `LayerLayout.*` | D | Layer row/group/child files | All geometry uses tokens | Migrate all geometry |
| `LayerHierarchy.*` geometry | D | `SMixtormatLayerHierarchy.cpp`, `...Connector.cpp` | Uses legacy indent/gap | Migrate reader |
| `Button.Height` | D | Tab/group action/segment constructors | Uses `GroupButtonHeight` token | Use theme button height |
| `Button.HorizontalPadding` | F | No `Theme.Button.HorizontalPadding` reader | Field is never used | Wire or remove |
| `MenuLayout.*` | B | Menu builder/item construction | Read from resolved style once | Working after rebuild; popup must reopen |
| `PreviewLayout.OverlayClusterInset` | F | No reader | No cluster-ground/container exists | Add cluster container or remove |
| `Preview.Plate*` | B | `SMixtormatPreviewPlate` | Only plates using the new widget update | Migrate remaining legacy plate |
| `Preview.IconRestOpacity` | E/G | Preview label paths | Label wording says “Icon”; also duplicates icon-role intent | Rename/split ownership |
| `GalleryLayout.*` | B/C | Tile/library construction | Read during construction | Works after workspace rebuild |
| `Shell.*` | B | Shell construction | Resolved fields are used | Works after rebuild |
| `Palette.Panel` | F | No Stage‑9 production reader found | Legacy palette/styles paint panel surfaces | Migrate or remove |
| `Palette.Shell` | D | `FMixtormatStyle::Refresh` | Legacy `MixtormatPalette::Shell()` wins | Migrate shell brushes |
| `Palette.OverlayGround` | F | None | Exists in theme, intentionally omitted from schema | Add overlay ground recipe |
| all typography except `GalleryCaption` | D/F | Legacy style-set text entries | Roles resolve but are not consumed | Migrate roles |
| most icon `ButtonSize` / `HitSize` | F/D | No role readers or token readers | Schema claims separate geometry | Add role-aware widgets |
| `Icons.LayerEye.*` | D | Layer rows use `LayerEyeSize` token | Does not affect eye geometry | Use icon role |
| `Icons.LayerDisclosure.*` | D | Layer rows use `FoldoutIconSize` token | Wrong role used | Use LayerDisclosure |
| `Icons.FoldoutDisclosure.*` | D | Inspector group uses token | Theme role unused | Use role |
| `Icons.CardLeading.*` | F | No direct role reader | Card header delegates caller content | Remove or define consumer |
| `Icons.GalleryToolbar.*` | F | No direct role reader | Library uses `PanelToolbar` | Remove or retarget |
| `Icons.TopBar.ButtonSize/HitSize/Hover/Disabled` | F | Topbar uses glyph/rest only | Most exposed fields have no reader | Add role-aware button or remove |
| `Icons.PreviewToolbar.HitSize` | F | No reader | `ButtonSize` is used instead | Define hit geometry or remove |
| locator for Controls/Typography/Global | H | `MixtormatStyleLocator.cpp:37-41,129-154` | Type matching targets unrelated nested widgets | Property metadata |

## Working properties

### A. Live paint readers, but not guaranteed immediate invalidation

These read Stage‑9 data during `OnPaint`:

- **Well recipe**  
  `Well.Radius`, `ShadeBlend`, `ShadeTop`, `ShadeBottom`, all border opacities,  
  `BorderWidth`, `BorderSaturation`, `HoverLiftOpacity`  
  → `MixtormatRecipes.cpp:38-85,547-560`  
  → `MixtormatWell.cpp:18-94`, slider/toggle well painting.

- **Fill recipe**  
  All `Fill.*` fields  
  → `MixtormatRecipes.cpp:570-634`  
  → `SMixtormatSlider.cpp:488-500` and checked-toggle recipe.

- **Foldout surface**  
  All `Foldout.*` paint fields and `FoldoutLayout.Radius`  
  → `MixtormatRecipes.cpp:256-297`  
  → `SMixtormatFoldoutHeader.cpp:43-47`.

- **Card surface**  
  `Card.Blend`, opacity, saturation, falloff, reach, radius  
  → `MixtormatRecipes.cpp:299-363`  
  → `SMixtormatInspectorCard.cpp:266-306`.

- **Layer paint**  
  Most `Layer.*` color, blend, saturation, strength, glow fields  
  → `MixtormatRecipes.cpp:137-247`  
  → `SMixtormatLayerSurface.cpp`.

- **Button recipe**  
  `Button.BodyBlend`, gradients, hairline, separator, text opacity  
  → `MixtormatRecipes.cpp:684-778`  
  → `MixtormatGroupButton.cpp:93-129`.

- **Menu paint**  
  `Menu.*` surface and row colors/opacities  
  → `MixtormatRecipes.cpp:365-452`  
  → `SMixtormatMenuPanel.cpp`, `SMixtormatMenuItem.cpp`.

- **Gallery surface**  
  `Gallery.*` paint fields  
  → `MixtormatRecipes.cpp:454-498`  
  → `SMixtormatTile.cpp:174-195`.

### B. Rebuild-dependent but working

- `MenuLayout.*` is captured by `MixtormatMenuBuilder.cpp:108-171` and
  `SMixtormatMenuItem.cpp:18-133`.
- `PreviewLayout.OverlayInset`, `ToolbarGap`, `OverlayButtonGap`,
  `ComparisonToggleGap`, `ResolutionControlWidth`, `TogglePadding`
  are captured in `SMixtormat_Preview.cpp`.
- `GalleryLayout.TileGap`, `TilePadding`, `CaptionHeight`, `CaptionInset`,
  `OverlayInset`, `HeaderGap` are captured in tile/library construction.
- `Shell.TopBarHeight`, `StatusBarHeight`, `PanelPadding`,
  `SplitterVisualWidth`, `SplitterHitWidth`, and `ShellTheme.*`
  are used in `SMixtormat_Shell.cpp`.

All currently rebuild because `ApplyPendingTheme()` tears down and rebuilds the
workspace in `SMixtormat_Theme.cpp:128-167`.

## Typography

### Actual role audit

| Role | Actual Stage‑9 consumer | Current result | Locator now | Correct target |
|---|---|---|---|---|
| Body | None | F | Controls | All body text once migrated |
| ControlLabel | None | F | Controls | Slider labels, toggle labels, chip labels |
| ControlValue | None | F | Controls | Slider numeric values and entries |
| Caption | None | F | Controls | Captions, section labels, status text |
| FoldoutTitle | None | D | Foldout | Actual foldout title/header |
| CardTitle | None | D | Card | Card-title text only |
| LayerName | None | D | Layer | Layer/group/child names |
| LayerSource | None | D | Layer | Layer source/kind/count text |
| Menu | None | D | Menu | Menu labels |
| MenuShortcut | None | D | Menu | Shortcut text |
| GalleryCaption | `SMixtormatTile.cpp:35-38` | C/B | Gallery | Tile badge/name captions |
| TopBar | None | F | Button | Topbar document and action labels |
| PreviewLabel | None | F | Preview | Preview cluster labels/FOV/readouts |
| Badge | None | D | Layer | Badge text only |

### Actual text path

- `ResolveTheme()` copies every role into resolved state:
  `MixtormatResolvedStyle.cpp:488-509`.
- `FMixtormatTypography::MakeTextStyle()` is functional:
  `MixtormatTypography.cpp:65-75`.
- The only production call to `GetSpec()` is `GalleryCaption`:
  `SMixtormatTile.cpp:35-38`.
- Everything else is built from legacy token values inside
  `FMixtormatStyle::Refresh()`:
  `MixtormatStyle.cpp:213-830`.
- Widgets then take raw `FTextBlockStyle*` pointers or copy fonts at construct:
  - foldout: `SMixtormatInspectorGroup.cpp:140-165`
  - cards: `SMixtormatInspectorCard.cpp:31-43`
  - layers: `SMixtormatLayerRow.cpp:134-164`
  - menus: `SMixtormatMenuItem.cpp:54-71`
  - topbar: `SMixtormat_Shell.cpp:113-124`
  - sliders: `SMixtormatSlider.cpp:52-71,544-547`

### Conclusion

`FMixtormatStyle::Refresh()` regenerates **legacy** text styles, not resolved
`EMixtormatTextRole` styles. Workspace rebuild recreates relevant widgets, but
the recreated widgets still receive legacy styles.

### Locator recommendation

Replace `TargetFor()` string/tab inference with property metadata:

```cpp
EMixtormatStyleTarget Target;
EMixtormatSemanticTarget SemanticTarget;
EMixtormatTextRole TextRole;
EMixtormatRefreshMode RefreshMode;
```

For typography, locate all registered semantic consumers of the role:
`LayerName` should blink layer, group, and child name text; not generic controls.

## Controls / Toggle / Well / Fill

### Toggle

- The little square toggle does use the shared Stage‑9 well/fill paint recipes.
- Its geometry and disabled shade are still legacy:
  - `Toggle.Size` → token at `SMixtormatToggle.cpp:37-38`
  - `Toggle.FillInset` → token at `:55`
  - fill size → derived legacy `ToggleFillSize` at `:61-62`
  - disabled shade → tokens at `:44-45`
- Thus its **paint vocabulary** is shared and live-after-repaint, but its
  exposed toggle-specific geometry is legacy-overridden.

### Well / Fill ownership

- **Well only:** radius, recess blend, recess ramp, border width/opacities,
  border saturation, hover lift.
- **Fill only:** blend modes, body ramps, state saturation, disabled fill,
  horizontal shade ramp and midpoint.
- **Toggle only:** outer square size, fill inset, disabled well shade.
- Toggle intentionally omits the Fill horizontal shade:
  `MixtormatRecipes.cpp:678-680`.

### Border capability

The generic border model can express all required edge combinations:

```cpp
bTop
bBottom
bLeft
bRight
```

`FMixtormatBorderLayer` declares them in `MixtormatTheme.h:166-169`, and
`FMixtormatSurfacePainter` paints individual edges.

Current recipe intent:

| Component | Actual edges |
|---|---|
| Well | Top + Bottom only |
| Toggle | Inherits Well: Top + Bottom only |
| Group button hairline | Top only |
| Group button separator | Right only |
| Foldout | Top only |
| Menu | Full perimeter |
| Gallery tile | Full perimeter; selected edge is top |
| Card | No generic recipe border |

UI STYLE cannot author per-edge switches because the schema exposes numeric
opacities only. That is appropriate for shared wells. Toggle should remain a
fixed `Top + Bottom, no sides` recipe unless the prototype explicitly introduces
a toggle-specific edge variant.

## Buttons / Tabs / Segmented

### Stage‑9 group-button recipe

`MakeButtonRecipe()` is correct about the intended shared visual:

- body gradient
- top-only hairline
- optional right-edge separator
- no full rectangular side/bottom border
- no pressed padding offset

References: `MixtormatRecipes.cpp:684-765`.

### Production split

- `SMixtormatSegmentedControl` uses the recipe surface:
  `SMixtormatSegmentedControl.cpp:40-56`.
- Group-style tab strips use the recipe surface:
  `SMixtormatTabStrip.cpp:59-77`.
- `SMixtormatGroupAction` uses the recipe surface:
  `SMixtormatGroupAction.h:43-51`.

But their geometry is legacy:

- group height: `MixtormatTokens::GroupButtonHeight`
- group padding: `MixtormatTokens::GroupButtonPaddingHorizontal`
- tab width/height: `MixtormatTokens::TabWidth/TabHeight`
- non-group tabs still use legacy `TabToggle`, tab brushes, underline brushes:
  `SMixtormatTabStrip.cpp:82-138`.
- topbar actions still use legacy `Mixtormat.TopButton`:
  `SMixtormat_Shell.cpp:95-314`.

So the shared recipe is correct, but not all tabs/topbar callers are on it.

## Preview overlays

### Individual control plates

Implemented:

- `Preview.PlateSource`
- `Preview.PlateOpacity`
- `Preview.HoverAccent`
- `Preview.PressAccent`

Reader:

- `MakePreviewPlateRecipe()` in `MixtormatRecipes.cpp:510-545`
- `SMixtormatPreviewPlate::OnPaint()` in `SMixtormat_Preview.cpp:38-59`

Actual plate coverage:

| Cluster | Current background |
|---|---|
| Top-left render settings | No cluster ground; child segmented/slider chrome |
| Top-center before/after/bypass | Individual `SMixtormatPreviewPlate` controls |
| Top-center Final popup | Legacy `Mixtormat.ViewportOverlayButton` plate |
| Left lighting rail | Individual `SMixtormatPreviewPlate` controls |
| Right geometry rail | Individual `SMixtormatPreviewPlate` controls |
| Bottom-left scene settings | No cluster ground; segmented/toggle/slider chrome |
| Bottom-right output controls | Legacy `Mixtormat.TopButton` plus segmented |
| Bottom-center camera/FOV | No cluster ground; label/slider only |

### Whole overlay / cluster ground

Not implemented.

- HTML has separate:
  - `--overlay-top-rgb`
  - `--overlay-bottom-rgb`
  - `--overlay-ground-opacity`
- Stage‑9 has only `Palette.OverlayGround`.
- `OverlayGround` is not exposed in schema by design:
  `MixtormatThemeSchema.cpp:203`.
- No Stage‑9 recipe or `SMixtormatPreviewCluster` paints a broader ground.
- `PreviewLayout.OverlayClusterInset` has no production reader.

This is a genuine missing prototype feature, not a bad numeric value.

## Icons

| Role | Production reader | Status |
|---|---|---|
| TopBar | `SMixtormat_Shell.cpp:20-33` | Glyph/rest only; size is construction-cached |
| PanelToolbar | `SMixtormat_Library.cpp:519-595` | Glyph/rest only; cached |
| PreviewToolbar | `SMixtormat_Preview.cpp:86-97,1084-1244` | Glyph/button/rest/hover; cached geometry |
| LayerEye | None | Legacy `LayerEyeSize`, layer icon tokens |
| LayerDisclosure | None | Uses legacy `FoldoutIconSize` |
| FoldoutDisclosure | None | Uses legacy foldout icon tokens |
| CardLeading | None | No semantic card-leading role consumer |
| Menu | `SMixtormatMenuItem.cpp:37-47,156-169` | Glyph/rest works; cached geometry |
| GalleryToolbar | None | Library uses `PanelToolbar` instead |

All `SBox::WidthOverride(float)` / `HeightOverride(float)` role reads are
construction-cached. They need reconstruction unless changed to attributes.

## Layout

### Genuine resolved layout readers

- Menu layout: builder/item construction.
- Preview layout: preview construction.
- Gallery layout: tile/library construction.
- Shell layout: shell construction.

All are **rebuild-dependent** and the current workspace refresh handles them.

### Legacy-overridden layout fields

- All controls layout except `DisabledLabelOpacity`.
- Most foldout layout.
- Most card layout.
- All layer layout.
- Button height/padding.
- Tab strip geometry.
- Toggle geometry.
- Chip geometry and chevron size.

Examples:

- `SMixtormatSlider.cpp:168-171` reads legacy `RowFieldMinWidth/RowHeight`.
- `SMixtormatChip.cpp:27-89` reads legacy chip/chevron dimensions.
- `SMixtormatInspectorGroup.cpp:103-305` reads foldout tokens.
- `SMixtormatInspectorCard.cpp:82-206` reads card tokens.
- `SMixtormatLayerRow.cpp:45-205` reads layer tokens.
- `SMixtormatLayerGroupRow.cpp:65-163` reads layer tokens.
- `SMixtormatLayerChildRow.cpp:51-157` reads layer tokens.

## Persistence / defaults

### Current startup behavior

1. `FMixtormatStyle::Initialize()` runs old `FMixtormatLiveTheme::Initialize()`.
2. It then loads old `LiveTheme`:
   `MixtormatStyle.cpp:95-104`.
3. `FMixtormatThemeStore::EnsureInitialised()` creates `MakeDefaultTheme()`.
4. It validates/resolves only that default:
   `MixtormatThemeStore.cpp:17-25`.
5. `UIStyleTheme.json` is loaded only from the UI STYLE panel Load button:
   `SMixtormatLiveThemePanel.cpp:572-585`.

Therefore saved new-schema UI STYLE data is never auto-loaded.

### Desired semantics

| Event | Recommended behavior |
|---|---|
| Startup with valid JSON | Load `UIStyleTheme.json` before first `FMixtormatStyle::Refresh()` |
| Startup without JSON | Use compiled `MakeDefaultTheme()` |
| Startup with malformed JSON | Use compiled fallback; report non-blocking warning |
| Reset Property | Reset that field to compiled factory default |
| Reset All | Set full compiled factory default; do not delete JSON automatically |
| Save | Persist current authored theme |
| Delete JSON | Next startup uses compiled fallback |
| Explicit Load | Optional reload-from-disk action, not required for normal startup |

Loading before Stage‑9 style initialization is safe only after removing or
migrating old-style ownership. Today `FMixtormatStyle::Refresh()` is legacy
token/palette-based, so loading Stage‑9 first does not make the style set use it.

## Locator

Current locator is structurally unreliable.

- It identifies target widgets from `Widget->GetType()`:
  `MixtormatStyleLocator.cpp:29-74`.
- It derives the target from tab or property-ID string:
  `MixtormatStyleLocator.cpp:110-159`.
- It stops walking beneath the first matching widget:
  `MixtormatStyleLocator.cpp:85-91`.

Known false/misleading mappings:

- **Controls** locates sliders, toggles, wells, and inspector wells together.
- A toggle contains `SMixtormatWellBox`; a chip also contains one.
- **Button** conflates shared group buttons, topbar actions, tabs, segments.
- **Preview** conflates plate controls and the entire preview viewport.
- **Menu** conflates menu panels and every menu row.
- **Layer** conflates row surfaces, rows, groups, and children.
- **Typography Body/ControlLabel/ControlValue/Caption** always maps to Controls.
- **Typography TopBar** maps to Button, not topbar text.
- **Palette.Global** maps the whole `SMixtormat` root.
- **Shell** also maps the whole `SMixtormat` root.

Recommendation: put semantic metadata directly on
`FMixtormatThemeProperty`; do not reverse-guess from tab/ID.

## Remaining legacy readers

### Actively blocks UI STYLE

- `FMixtormatStyle::Refresh()`:
  `MixtormatStyle.cpp:106-953`
  - direct `MixtormatTokens`
  - direct `MixtormatPalette`
  - old widget styles/brushes
  - legacy typography
  - legacy toggle, tab, topbar, overlay brushes

- Legacy primitive paths still present:
  - `SMixtormatGradientBox`
  - `MixtormatGradientPainter`
  - `MixtormatGroupCardPainter`
  - legacy `MixtormatPalette`
  - old `MixtormatLiveTheme`

### Construction-cached styles

- `SMixtormatInspectorGroup.cpp:140-165`
- `SMixtormatInspectorCard.cpp:31-43`
- `SMixtormatTile.cpp:35-38`
- `SMixtormatMenuItem.cpp:37-110`
- `SMixtormat_Preview.cpp:1084-1244`
- `SMixtormat_Shell.cpp:83-314`
- `SMixtormatTabStrip.cpp:41-145`
- `SMixtormatGroupAction.h:19-64`

### Harmless/non-UI legacy use

- Thumbnail render target background in
  `Services/MixtormatThumbnailRenderer.cpp:477-543` is render content,
  not Slate chrome.

## Live refresh audit

| Edit path | Store update | Visible refresh |
|---|---|---|
| Number spin drag | Immediate `SetTheme` | Coalesced rebuild after `0.1s` |
| Number typed commit | `CommitTheme` | Coalesced rebuild after `0.1s` |
| Bool | `CommitTheme` | Coalesced rebuild after `0.1s` |
| Choice | `CommitTheme` | Coalesced rebuild after `0.1s` |
| Color picker | Continuous commit callback | Coalesced rebuild after `0.1s` |
| Save | No visual change | Writes current theme only |
| Load | `SetTheme` | Coalesced rebuild after `0.1s` |
| Reset Property/All | `SetTheme` | Coalesced rebuild after `0.1s` |

Important details:

- `PreviewNumber()` updates the store immediately:
  `SMixtormatLiveThemePanel.cpp:439-458`.
- It does **not** explicitly invalidate all Stage‑9 paint widgets.
- Any in-place Stage‑9 paint visibility before rebuild relies on incidental Slate
  repaint, so it is not guaranteed.
- The `0.1s` timer always reconstructs the workspace:
  `SMixtormat_Theme.cpp:128-167`.
- Popup/menu widgets are outside the rebuilt workspace lifecycle.
- Open menus may retain construction-captured layout/text until reopened.
- UI STYLE itself is not reconstructed; its own generic Slate controls are not
  styled by the production theme.

### Recommended refresh modes

```cpp
enum class EMixtormatThemeRefreshMode : uint8
{
    Paint,
    Layout,
    StyleRefresh,
    Reconstruct
};
```

Suggested use:

- **Paint:** recipes, palette colors, opacity, ramps, borders.
- **Layout:** attribute-backed sizes/padding where possible.
- **StyleRefresh:** legacy Slate style-set consumers during migration.
- **Reconstruct:** copied fonts, `SBox` float overrides, popup/menu rebuilds.

Do not use rebuild for every paint edit once semantic invalidation exists.

## Schema quality

### Remove or defer until real readers exist

- `Button.HorizontalPadding`
- `PreviewLayout.OverlayClusterInset`
- unused icon role fields, especially `CardLeading`, `GalleryToolbar`
- all typography roles without a production reader
- all layout fields still represented only by tokens

### Rename

- `Preview.IconRestOpacity` → `Preview.LabelRestOpacity` if it remains separate.
- Or remove it and use `PreviewToolbar.RestOpacity` consistently.

### Make fixed recipe behavior

- Well/toggle border edge selection.
- Group-button top hairline and right separator edge selection.
- These are component design rules, not four authoring toggles per component.

### Add only after implementation

- Preview cluster ground:
  - top RGB
  - bottom RGB
  - opacity
  - likely a dedicated `Preview.OverlayGround` recipe.
- Semantic locator metadata.
- Refresh mode metadata.
- Explicit role-to-consumer registration.

## Recommended repair order

1. Establish Stage‑9 ownership for every already-exposed property.
2. Remove schema fields with no reader or migrate their real token reader.
3. Auto-load valid `UIStyleTheme.json` before first style/workspace build.
4. Define factory-default versus saved-authored-theme reset semantics.
5. Add `Paint`, `Layout`, `StyleRefresh`, `Reconstruct` metadata.
6. Replace full rebuild-on-every-edit with targeted refresh/invalidation.
7. Migrate typography roles from legacy style-set/token construction.
8. Migrate icon geometry and opacity to semantic icon-role consumers.
9. Finish toggle geometry and disabled shade migration.
10. Migrate non-group tabs/topbar and remaining legacy overlay controls.
11. Add preview cluster-ground widgets and fields.
12. Replace locator inference with property semantic metadata.
13. Remove dead schema entries after every reader is confirmed.
14. Only then perform Stage‑10 legacy deletion.

## Exact files for the repair pass

### Theme, persistence, schema, refresh

- `Source/MixtormatEditor/Private/Style/MixtormatThemeSchema.h`
- `Source/MixtormatEditor/Private/Style/MixtormatThemeSchema.cpp`
- `Source/MixtormatEditor/Private/Style/MixtormatThemeStore.cpp`
- `Source/MixtormatEditor/Private/Style/MixtormatStyle.cpp`
- `Source/MixtormatEditor/Private/Style/MixtormatStyleLocator.h`
- `Source/MixtormatEditor/Private/Style/MixtormatStyleLocator.cpp`
- `Source/MixtormatEditor/Private/Widgets/SMixtormatLiveThemePanel.cpp`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Theme.cpp`

### Controls and shared recipes

- `Source/MixtormatEditor/Private/Style/MixtormatRecipes.cpp`
- `Source/MixtormatEditor/Private/Style/MixtormatGroupButton.cpp`
- `Source/MixtormatEditor/Private/UI/Atoms/SMixtormatToggle.cpp`
- `Source/MixtormatEditor/Private/UI/Atoms/SMixtormatChip.cpp`
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatSlider.cpp`
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatSegmentedControl.cpp`
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatTabStrip.cpp`
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatGroupAction.h`
- `Source/MixtormatEditor/Private/UI/Primitives/MixtormatWell.cpp`
- `Source/MixtormatEditor/Private/UI/Primitives/SMixtormatWellBox.cpp`

### Typography, icons, containers, preview

- `Source/MixtormatEditor/Private/Style/MixtormatTypography.cpp`
- `Source/MixtormatEditor/Private/UI/Containers/SMixtormatInspectorGroup.cpp`
- `Source/MixtormatEditor/Private/UI/Containers/SMixtormatInspectorCard.cpp`
- `Source/MixtormatEditor/Private/UI/Layers/SMixtormatLayerRow.cpp`
- `Source/MixtormatEditor/Private/UI/Layers/SMixtormatLayerGroupRow.cpp`
- `Source/MixtormatEditor/Private/UI/Layers/SMixtormatLayerChildRow.cpp`
- `Source/MixtormatEditor/Private/UI/Layers/SMixtormatLayerIcon.cpp`
- `Source/MixtormatEditor/Private/UI/Menus/MixtormatMenuBuilder.cpp`
- `Source/MixtormatEditor/Private/UI/Menus/SMixtormatMenuItem.cpp`
- `Source/MixtormatEditor/Private/UI/Controls/SMixtormatTile.cpp`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Preview.cpp`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Shell.cpp`
- `Source/MixtormatEditor/Private/Widgets/SMixtormat_Library.cpp`

No files were edited. No build, tests, Unreal launch, Git operation, or automated analysis was run.