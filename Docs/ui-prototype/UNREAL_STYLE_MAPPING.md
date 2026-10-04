# CSS → Unreal style mapping

Parity checklist for the style rewrite. Every row is a token authored in
`Docs/ui-prototype/tokens.css` or `components.css` and the place the value now lives in the
Unreal side. The prototype is the visual specification; where the two differ, the difference is
recorded here rather than left to be discovered in the viewport.

## Reading the table

- **Unreal** — the field in the new editable theme (`FMixtormatTheme`, `Style/MixtormatTheme.h`).
- **Resolved** — the field in `FMixtormatResolvedStyle` that a painter actually reads.
- **Stage** — the rewrite stage that lands the reader. `—` means the value exists in the model but
  no production reader consumes it yet, which is the only honest state until the widget is
  migrated.

## Palette

| CSS | Unreal | Stage |
|---|---|---|
| `--ground-rgb` | `Palette.Ground` | — |
| — (shell column, no authored token) | `Palette.Shell` | — |
| `--panel-rgb` | `Palette.Panel` | — |
| `--text-rgb` | `Palette.Text` | — |
| `--text-muted-opacity` | `Palette.TextMuted` (opacity folded in) | — |
| `--accent-rgb` | `Palette.Accent` | — |
| `--modified-rgb` | `Palette.Modified` | — |
| `--warning-rgb` | `Palette.Warning` | — |
| — (no authored token) | `Palette.Error` | — |
| `--shade-rgb` | `Palette.Shade` | — |
| `--hairline-rgb` | `Palette.Hairline` | — |
| `--popup-bottom-rgb` | `Palette.MenuGround` | — |
| — (thumbnail plate, no authored token) | `Palette.ThumbnailGround` | — |
| `--overlay-bottom-rgb` | `Palette.OverlayGround` | — |

## Wells

| CSS | Unreal | Resolved | Painter |
|---|---|---|---|
| `--well-blend-mode` | `Well.ShadeBlend` | `Well.Shade` | `MixtormatSurfacePainter` |
| `--well-shade-top` / `-bottom` | `Well.ShadeTop` / `ShadeBottom` | `Well.Shade` ramp | `MixtormatSurfacePainter` |
| `--well-border-width` | `Well.BorderWidth` | `Well.BorderWidth` | `MixtormatSurfacePainter` |
| `--well-border-opacity` | `Well.BorderOpacity` | `Well.Border` ramp | `MixtormatSurfacePainter` |
| `--well-border-top-opacity` | `Well.BorderTopOpacity` | `Well.Border` stop 0 | `MixtormatSurfacePainter` |
| `--well-border-bottom-opacity` | `Well.BorderBottomOpacity` | `Well.Border` stop 1 | `MixtormatSurfacePainter` |
| `--well-border-hover-opacity` | `Well.BorderHoverOpacity` | `Well.BorderHover` | `MixtormatSurfacePainter` |
| `--well-border-hover-top-opacity` | `Well.BorderHoverTopOpacity` | `Well.BorderHover` stop 0 | `MixtormatSurfacePainter` |
| `--well-border-hover-bottom-opacity` | `Well.BorderHoverBottomOpacity` | `Well.BorderHover` stop 1 | `MixtormatSurfacePainter` |
| `--well-border-saturation` | `Well.BorderSaturation` | folded into `Well.Border` | `MixtormatSurfacePainter` |
| `--hover-lift-opacity` | `Well.HoverLiftOpacity` | — | — |
| `--well-radius` | `Well.Radius` | `Well.Radius` | `MixtormatSurfacePainter` |

## Fill

| CSS | Unreal | Resolved | Painter |
|---|---|---|---|
| `--surface-blend-mode` | `Fill.BodyBlend` | `Fill.Body*` | `MixtormatSurfacePainter` |
| `--fill-body-top` / `-bottom` | `Fill.Top` / `Bottom` | `Fill.Body` ramp | `MixtormatSurfacePainter` |
| `--fill-body-hover-top` / `-bottom` | `Fill.HoverTop` / `HoverBottom` | `Fill.BodyHover` ramp | `MixtormatSurfacePainter` |
| `--fill-body-active-top` / `-bottom` | `Fill.ActiveTop` / `ActiveBottom` | `Fill.BodyActive` ramp | `MixtormatSurfacePainter` |
| `--fill-saturation` | `Fill.Saturation` | `Fill.Body` | `MixtormatSurfacePainter` |
| `--fill-hover-saturation` | `Fill.HoverSaturation` | `Fill.BodyHover` | `MixtormatSurfacePainter` |
| `--fill-active-saturation` | `Fill.ActiveSaturation` | `Fill.BodyActive` | `MixtormatSurfacePainter` |
| `--fill-falloff-power` | `Fill.FalloffPower` | `Fill.FalloffPower` | `MixtormatSurfacePainter` |
| `--well-blend-mode` (shade pass) | `Fill.ShadeBlend` | `Fill.Shade` | `MixtormatSurfacePainter` |
| `--fill-shade-start` / `-mid` / `-end` | `Fill.ShadeStart` / `ShadeMid` / `ShadeEnd` | `Fill.Shade` 3 stops | `MixtormatSurfacePainter` |
| `--fill-shade-mid-position` | `Fill.ShadeMidPosition` | `Fill.ShadeMidPosition` | `MixtormatSurfacePainter` |

## Toggle

| CSS | Unreal | Resolved | Painter |
|---|---|---|---|
| `--toggle-size` | `Toggle.Size` | `Toggles.Size` | — |
| `--toggle-fill-inset` | `Toggle.FillInset` | `Toggles.FillInset` | — |
| `--toggle-disabled-shade-top` | `Toggle.DisabledShadeTop` | `Toggles.DisabledShadeTop` | — |
| `--toggle-disabled-shade-bottom` | `Toggle.DisabledShadeBottom` | `Toggles.DisabledShadeBottom` | — |

The toggle owns no surface: checked is `Toggles.Well` plus `Toggles.Fill`, which are copies of the
well and fill recipes. That reuse is the point of the recipe families, not an optimisation.

## Foldouts

| CSS | Unreal | Resolved | Painter |
|---|---|---|---|
| `--foldout-blend-mode` | `Foldout.LiftBlend` | `Foldouts.Lift` | `MixtormatFoldoutPainter` |
| `--foldout-accent-blend-mode` | `Foldout.AccentBlend` | `Foldouts.Accent` | `MixtormatFoldoutPainter` |
| `--foldout-falloff-power` | `Foldout.LiftFalloff.Power` | `Foldouts.Lift` | `MixtormatFoldoutPainter` |
| `--header-tint-rgb` | `Foldout.LiftTint` | `Foldouts.Lift` | `MixtormatFoldoutPainter` |
| `--header-tint-opacity` | `Foldout.LiftOpacity` | `Foldouts.Lift` | `MixtormatFoldoutPainter` |
| `--header-hover-rgb` | `Foldout.HoverTint` | `Foldouts.LiftHover` | `MixtormatFoldoutPainter` |
| `--header-hover-opacity` | `Foldout.HoverTintOpacity` | `Foldouts.LiftHover` | `MixtormatFoldoutPainter` |
| `--foldout-saturation` | `Foldout.LiftSaturation` | `Foldouts.Lift` | `MixtormatFoldoutPainter` |
| `--foldout-hover-saturation` | `Foldout.HoverSaturation` | `Foldouts.LiftHover` | `MixtormatFoldoutPainter` |
| `--foldout-accent-multiply-opacity` | `Foldout.AccentOpacity` | `Foldouts.Accent` | `MixtormatFoldoutPainter` |
| `--foldout-accent-hover-multiply-opacity` | `Foldout.AccentHoverOpacity` | `Foldouts.AccentHover` | `MixtormatFoldoutPainter` |
| `--foldout-hairline-opacity` | `Foldout.HairlineOpacity` | `Foldouts.Hairline` | `MixtormatFoldoutPainter` |
| `--hairline-hover-opacity` | `Foldout.HairlineHoverOpacity` | `Foldouts.HairlineHover` | `MixtormatFoldoutPainter` |
| `--foldout-hairline-saturation` | `Foldout.HairlineSaturation` | `Foldouts.Hairline` | `MixtormatFoldoutPainter` |
| `--foldout-hairline-hover-saturation` | `Foldout.HairlineHoverSaturation` | `Foldouts.HairlineHover` | `MixtormatFoldoutPainter` |
| `--foldout-height` … `--foldout-header-padding-bottom` | `FoldoutLayout.*` | `FoldoutLayout.*` | layout |

**Local, not palette.** `--header-tint-rgb` / `--header-hover-rgb` map to fields on
`FMixtormatFoldoutTheme`, not to palette roles. They are authored per surface in the prototype and
no other surface reads them, so a global role would let a well, a card or a menu claim a colour
that only means "this foldout header's lift". The palette stays small only while colours that mean
one thing stay local to the one thing that uses them.

`--header-tint-rgb` and `--header-tint-opacity` are carried as a colour and a separate weight
rather than one premultiplied value, because the prototype authors them as two tokens and merging
them would make one of them unauthorable.

## Group cards

| CSS | Unreal | Resolved | Painter |
|---|---|---|---|
| `--card-blend-mode` | `Card.Blend` | `Cards.Gradient` | `MixtormatGroupCardPainter` |
| `--card-header-opacity` | `Card.HeaderOpacity` | `Cards.Gradient` | `MixtormatGroupCardPainter` |
| `--card-body-opacity` | `Card.BodyOpacity` | `Cards.Gradient` floor | `MixtormatGroupCardPainter` |
| `--card-header-saturation` | `Card.HeaderSaturation` | `Cards.Gradient` | `MixtormatGroupCardPainter` |
| `--card-body-saturation` | `Card.BodySaturation` | `Cards.Gradient` | `MixtormatGroupCardPainter` |
| `--card-falloff-power` | `Card.FalloffPower` | `Cards.FalloffPower` | `MixtormatGroupCardPainter` |
| `--card-gradient-reach` | `Card.Reach` | `Cards.Reach` | `MixtormatGroupCardPainter` |
| `--card-radius` | `Card.Radius` | `Cards.Radius` | `MixtormatGroupCardPainter` |
| `--card-header-*`, `--card-outer-*`, `--card-body-*` | `CardLayout.*` | `CardLayout.*` | layout |

## Layers

| CSS | Unreal | Resolved | Painter |
|---|---|---|---|
| `--layer-blend-mode` | `Layer.Blend` | `Layers.Row*` | `SMixtormatLayerSurface` |
| `--layer-group-blend-mode` | `Layer.GroupBlend` | `Layers.GroupCross` | `SMixtormatLayerSurface` |
| `--layer-saturation` | `Layer.RestSaturation` | `Layers.Row` | `SMixtormatLayerSurface` |
| `--layer-hover-saturation` | `Layer.HoverSaturation` | `Layers.RowHover` | `SMixtormatLayerSurface` |
| `--layer-selected-saturation` | `Layer.SelectedSaturation` | `Layers.RowSelected` | `SMixtormatLayerSurface` |
| `--layer-group-saturation` | `Layer.GroupSaturation` | `Layers.GroupCross` | `SMixtormatLayerSurface` |
| `--group-cross-opacity` | `Layer.GroupStrength` | `Layers.GroupCrossStrength` | `SMixtormatLayerSurface` |
| `--child-saturation` | `Layer.ChildSaturation` | `Layers.Child` | `SMixtormatLayerSurface` |
| `--child-hover-saturation` | `Layer.ChildHoverSaturation` | `Layers.ChildHover` | `SMixtormatLayerSurface` |
| `--child-selected-saturation` | `Layer.ChildSelectedSaturation` | `Layers.ChildSelected` | `SMixtormatLayerSurface` |
| `--child-right-opacity` | `Layer.ChildStrength` | `Layers.Child` | `SMixtormatLayerSurface` |
| `--child-hover-right-opacity` | `Layer.ChildHoverStrength` | `Layers.ChildHover` | `SMixtormatLayerSurface` |
| `--child-selected-right-opacity` | `Layer.ChildSelectedStrength` | `Layers.ChildSelected` | `SMixtormatLayerSurface` |
| `--layer-active-glow-opacity` | `Layer.ActiveGlow.Opacity` | `Layers.GlowOpacity` | `SMixtormatLayerSurface` |
| `--layer-active-glow-reach` | `Layer.ActiveGlow.Reach` | `Layers.GlowReach` | `SMixtormatLayerSurface` |
| `--layer-active-glow-saturation` | `Layer.ActiveGlow.Saturation` | `Layers.GlowSaturation` | `SMixtormatLayerSurface` |
| `--layer-active-hairline-width` | `Layer.ActiveGlow.HairlineWidth` | `Layers.GlowHairlineWidth` | `SMixtormatLayerSurface` |
| `--layer-active-hairline-opacity` | `Layer.ActiveGlow.HairlineOpacity` | `Layers.GlowHairline` | `SMixtormatLayerSurface` |
| `--layer-hierarchy-line-width` | `LayerHierarchy.Width` | `Layers.HierarchyWidth` | `MixtormatHierarchyPainter` |
| `--layer-hierarchy-line-opacity` | `LayerHierarchy.Opacity` | `Layers.HierarchyOpacity` | `MixtormatHierarchyPainter` |
| `--layer-indent` | `LayerHierarchy.Indent` | `LayerLayout.ChildIndent` | layout |
| `--layer-height`, `--child-height`, `--layer-group-height` | `LayerLayout.*` | `LayerLayout.*` | layout |

The rails resolve in the layer style but are painted by `MixtormatHierarchyPainter`, after the row
body and its glow. Folding them into the row surface would saturate them and multiply them through
the row gradient, which is the bug this separation exists to prevent.

## Buttons

| CSS | Unreal | Resolved | Painter |
|---|---|---|---|
| `--group-button-blend-mode` | `Button.BodyBlend` | `Buttons.Body*` | `SMixtormatGroupButtonSurface` |
| `--group-button-gradient-top` / `-bottom` | `Button.RestTop` / `RestBottom` | `Buttons.Body` | `SMixtormatGroupButtonSurface` |
| `--group-button-hover-gradient-top` / `-bottom` | `Button.HoverTop` / `HoverBottom` | `Buttons.BodyHover` | `SMixtormatGroupButtonSurface` |
| `--group-button-selected-gradient-top` / `-bottom` | `Button.SelectedTop` / `SelectedBottom` | `Buttons.BodySelected` | `SMixtormatGroupButtonSurface` |
| `--group-button-gradient-saturation` | `Button.GradientSaturation` | `Buttons.Body*` | `SMixtormatGroupButtonSurface` |
| — (additive, not authored) | `Button.HairlineBlend` | `Buttons.Hairline*` | `SMixtormatGroupButtonSurface` |
| `--group-button-hairline-width` | `Button.HairlineWidth` | `Buttons.HairlineWidth` | `SMixtormatGroupButtonSurface` |
| `--group-button-hairline-opacity` | `Button.HairlineOpacity` | `Buttons.Hairline` | `SMixtormatGroupButtonSurface` |
| `--group-button-hover-hairline-opacity` | `Button.HairlineHoverOpacity` | `Buttons.HairlineHover` | `SMixtormatGroupButtonSurface` |
| `--group-button-selected-hairline-opacity` | `Button.HairlineSelectedOpacity` | `Buttons.HairlineSelected` | `SMixtormatGroupButtonSurface` |
| `--group-button-hairline-saturation` | `Button.HairlineSaturation` | `Buttons.Hairline*` | `SMixtormatGroupButtonSurface` |
| `--group-button-separator-*` | `Button.Separator*` | `Buttons.Separator*` | `SMixtormatGroupButtonSurface` |
| `--group-button-height` | `Button.Height` | `Buttons.Height` | layout |

## Menus

| CSS | Unreal | Resolved | Painter |
|---|---|---|---|
| `--popup-bottom-rgb` | `Palette.MenuGround` | `Menus.Ground` | menu brush |
| `--popup-top-rgb` / `--popup-tint-opacity` | `Menu.LipHeight` / `LipTintOpacity` | `Menus.Lip` | menu brush |
| `--popup-border-opacity` | `Menu.BorderOpacity` | `Menus.Border` | menu brush |
| `--menu-width` | `Menu.Width` | `Menus.Width` | layout |
| `--menu-row-height` | `Menu.ItemHeight` | `Menus.ItemHeight` | layout |
| `--menu-padding` | `MenuLayout.PanelPadding` | `MenuLayout.*` | layout |
| `--text-disabled-opacity` | `Menu.ItemDisabledOpacity` | `Menus.ItemDisabled` | menu row |

## Preview / gallery / shell

| CSS | Unreal | Resolved | Painter |
|---|---|---|---|
| `--overlay-ground-opacity` | `Preview.OverlayPlateOpacity` | `Preview.OverlayPlateOpacity` | — |
| `--overlay-hover-accent` | `Preview.OverlayHoverAccent` | `Preview.HoverAccent` | — |
| `--overlay-press-accent` | `Preview.OverlayPressAccent` | `Preview.PressAccent` | — |
| `--gallery-tile-size` | `Gallery.TileSize` | `Gallery.TileSize` | — |
| `--gallery-gap` | `Gallery.TileGap` | `GalleryLayout.TileGap` | layout |
| `--gallery-swatch-radius` / `--thumbnail-radius` | `Gallery.CornerRadius` | `Gallery.CornerRadius` | — |
| `--splitter-size` | `Shell.SplitterVisualWidth` | `Shell.SeparatorWidth` | splitter |
| `--splitter-hit-size` | `Shell.SplitterHitWidth` | `Shell.SeparatorHitWidth` | splitter |
| `--left-width`, `--inspector-width`, `--gallery-height` | `Shell.*Seed` | `ShellLayout.*Seed` | initial ratio only |

Splitters stay a real `SSplitter`. The seeds initialise the ratio and do not constrain the user
afterwards, so no fixed width is exposed as an editable property.

## Icons

| CSS | Unreal |
|---|---|
| `--topbar-icon-size`, `--topbar-icon-opacity` | `Icons[TopBar]` |
| `--toolbar-icon-size`, `--toolbar-icon-opacity` | `Icons[PanelToolbar]` |
| `--overlay-icon-size`, `--overlay-icon-opacity` | `Icons[PreviewToolbar]` |
| `--layer-icon-size`, `--layer-icon-opacity` | `Icons[LayerEye]` |
| `--foldout-icon-size`, `--foldout-icon-opacity` | `Icons[FoldoutDisclosure]` |
| `--card-icon-size`, `--card-icon-opacity` | `Icons[CardLeading]` |
| `--menu-icon-size`, `--menu-icon-opacity` | `Icons[Menu]` |
| `--layer-visibility-size`, `--layer-visibility-radius` | `Icons[LayerEye]` |
| `--icon-hit-padding` | folded into each role's `HitSize` |

## Typography

| CSS | Unreal |
|---|---|
| `--font-family` | `Typography.Family` |
| `.brand { font-weight: 700 }` (components.css) | `Typography[TopBar].Weight` → **Bold (700)** |
| `--body-size` | `Typography[Body].Size` |
| `--control-label-size` / `-weight` / `-opacity` | `Typography[ControlLabel]` |
| `--control-label-tracking` | `Typography[ControlLabel].TrackingPx` |
| `--value-weight` | `Typography[ControlValue].Weight` → **SemiBold (600)** |
| `--control-value-opacity` | `Typography[ControlValue].Opacity` |
| `--caption-size` | `Typography[Caption].Size` |
| `--foldout-title-size` / `-weight` / `-tracking` / `-opacity` | `Typography[FoldoutTitle]` |
| `--card-title-size` / `-weight` / `-tracking` / `-opacity` | `Typography[CardTitle]` |
| `--group-card-title-*` | `Typography[CardTitle]` |
| `--layer-group-title-size` / `-weight` | `Typography[LayerName]` |
| `--menu-caption-letter-spacing`, `--layer-source-letter-spacing` | per-role `TrackingPx` |

Tracking is authored in CSS pixels everywhere and converted centrally to Slate's 1/1000 em at
font-construction time. No call site writes a raw Slate letter-spacing value.

### Font resource strategy

| Concern | Resolution |
|---|---|
| Files | `Resources/Fonts/Inter-Regular.ttf`, `Inter-SemiBold.ttf`, `Inter-Bold.ttf` |
| Source | Static instances generated from `Resources/Fonts/Inter.ttf` with `fontTools.varLib.instancer` (OFL variable face, `wght` 100–900, `opsz` 14–32) |
| Axis pinning | `wght` pinned per face, `opsz` pinned to 14 — its minimum. Mixtormat renders 8–11px, and leaving optical size live would mean the outlines were never resolved for the sizes actually used |
| License | `Resources/Fonts/Inter-OFL.txt` ships unmodified beside the faces |
| Path resolution | `IPluginManager::Get().FindPlugin("Mixtormat")->GetBaseDir() / "Resources" / "Fonts"` |
| Loading | `EFontLoadingPolicy::LazyLoad` — files are on disk in editor and packaged builds |
| Hinting | `EFontHinting::Default`, so each face's own hinting applies |
| Ownership | `FStandaloneCompositeFont` (derives `FCompositeFont` + `FGCObject`), held in a function-local static for module lifetime |
| Registration | **None.** `FSlateFontInfo::GetCompositeFont()` returns `CompositeFont.Get()` directly when `FontObject` is not an `IFontProviderInterface`, and `FCompositeFontCache` builds its typeface cache lazily from the pointer. There is no `AddFont` entry point to call |

`Inter.ttf` itself is **not loaded at runtime**. It is the provenance for the static faces only.

### Weights

The prototype's authored weights are 400, 600 and 700, and those are the centres of the shipped
faces — so an authored value lands on the face it asked for rather than on a midpoint between two.

| `EMixtormatFontWeight` | CSS weight | File | Typeface entry |
|---|---|---|---|
| `Regular` | 400 | `Inter-Regular.ttf` | `Regular` |
| `SemiBold` | 600 | `Inter-SemiBold.ttf` | `SemiBold` |
| `Bold` | 700 | `Inter-Bold.ttf` | `Bold` |

**Why SemiBold exists.** `tokens.css` authors `--value-weight: 600`. The previous two-face
Regular/Bold mapping split at 500, so every authored 600 resolved to Bold and rendered at 700 —
control values were visibly heavier than the design. `FMixtormatTypography::FromCssWeight` snaps
to the nearest shipped face instead.

**Why static files and not the variable font.** `Inter.ttf` is a variable face (`fvar`, `gvar`,
`avar`, `HVAR` — confirmed by reading the sfnt table directory) and contains all these instances.
UE 5.8 SlateCore has no variation-axis support of any kind: no `FT_Set_Var_Design_Coordinates`, no
weight axis, nothing under `SlateCore/Public` or `SlateCore/Private`. A typeface entry resolves to a
**file**, which is exactly how the engine builds its own default font (`Roboto-Regular.ttf`,
`Roboto-Bold.ttf`, … each appended as a separate entry). Pointing two typeface names at one
variable TTF would produce two entries with identical outlines.

## Deliberately not mapped

These tokens exist in the prototype and have **no** Unreal counterpart, on purpose. Each one is a
place the rewrite refuses to add a value.

| CSS | Why it is dropped |
|---|---|
| `--foldout-vignette-*`, `--panel-vignette-*`, `--card-vignette-*` | Vignette infrastructure is out of scope; the UI must read correctly without it. |
| `--overlay-control-width` | The overlay cluster is laid out from its items, not from a fixed width. |
| `--popup-shadow-opacity` | Drop shadow is Slate's own, not a composited layer. |
| `--dialog-*`, `--popover-*`, `--badge-width`, `--help-*` | Secondary surfaces, not part of this rewrite's scope. |
| `--well-border-rgb`, `--well-border-hover-rgb` | The well outline uses the `Hairline` role with `Well.BorderSaturation`, matching the shipped Unreal behaviour. Recorded so the difference from the prototype's darker outline is not mistaken for a bug. |