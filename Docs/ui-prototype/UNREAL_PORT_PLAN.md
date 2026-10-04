# Porting the prototype styling to Unreal

Plan only. No plugin source is changed by this document.

## Purpose

Describe how the CSS visual contract in `Docs/ui-prototype/` is translated into the
existing Slate style system, what the browser cannot express and Slate must compute
explicitly, and in what order the work is safe to land.

## Audit of the existing Unreal side

Already present and reusable:

| Area | Location | Note |
| --- | --- | --- |
| Geometry tokens | `Private/Style/MixtormatDesignTokens.h` | namespace-scope `inline float` and `constexpr float`; only the `inline` ones are mutable at runtime; `RecomputeDerived()` exists |
| Color roles | `Private/Style/MixtormatPalette.h` | semantic role functions, `ResolveColor` for live overrides |
| Live theme registry | `Private/Style/MixtormatLiveTheme.h/.cpp` | number/color entries with min, max, default; serialize/deserialize; save/load |
| Slate style set | `Private/Style/MixtormatStyle.cpp`, `MixtormatMutableStyleSet.h` | `Refresh()` rebuilds brushes and text styles in place |
| UI Style popup | `Private/Widgets/SMixtormatLiveThemePanel.cpp`, `SMixtormat_Theme.cpp` | already wired to the top bar's `UI STYLE` button |
| Linear gradients | `Private/UI/Primitives/MixtormatGradientPainter.h/.cpp` | `MixtormatGradient::Paint` with CSS orientation and sRGB interpolation |
| Gradient container | `Private/UI/Primitives/SMixtormatGradientBox.h/.cpp` | wraps content in a painted gradient |
| Icon brushes | `Private/Style/MixtormatStyle.cpp`, `Private/UI/Atoms/MixtormatIcons.cpp` | 57 PNGs registered by role name |
| Preview overlays | `Private/Widgets/SMixtormat_Preview.cpp` `BuildPreviewPanel()` | the edge clusters match; the plates do not (see gaps) |
| Layer stack | `Private/UI/Layers` | rows, group rows, children, badges, chevrons |
| Controls | `Private/UI/Controls`, `Private/UI/Rows` | slider, toggle, segmented, dropdown row/chip |
| State machinery | none | no `FCurveSequence`, no `FCurveHandle`, no per-frame timer. `RegisterActiveTimer` is used only as a debounce for theme refresh, preview compose and rename (`SMixtormat_Theme.cpp:131`, `SMixtormatPreviewViewport.cpp:596`, `MixtormatLayerActions.cpp:1352`). `Invalidate(EInvalidateWidgetReason::Paint)` appears only in `SMixtormatScalarRamp.cpp:186-305` |

Gaps confirmed during the audit:

- `MixtormatGradient::Paint` supports linear orientations only. `MakeGradient` takes an
  `EOrientation` and a corner-radius vector and nothing else
  (`MixtormatGradientPainter.cpp:88-95`); `AxisPoint` picks one axis
  (`:17-20`). The vignette, the radial falloff and any conic effect have no Slate
  equivalent yet.
- Saturation is not modelled, and it does **not** belong at palette read time. One
  source colour is rendered at several different saturations — `--accent-rgb` is the fill
  at `fill-saturation: 0.7`, the group button at `group-button-gradient-saturation: 1.5`,
  and the active glow at `layer-active-glow-saturation: 1.5` — so a saturate applied
  inside `MixtormatPalette::Accent()` would be wrong for at least two of the three.
  Saturation is a property of the individual paint layer or gradient stop.
- Soft-light and plus-lighter blend modes do not exist in Slate. Plus-lighter already has
  two working precedents — `GroupCardBackground` (`MixtormatPalette.h:47-50`) and the
  rail-button plate (`MixtormatStyle.cpp:311-314`) both add source at alpha — and
  multiply already has one, the second-axis black-at-alpha pass in
  `SMixtormatGradientBox` (`MixtormatGradientBox.h:49-52`). Soft-light has neither: it is
  a backdrop-relative operation, not an add, so both users
  (`--foldout-accent-blend-mode`, `--layer-group-blend-mode`, applied at
  `components.css:177` and `:90`) need an explicit compositing helper rather than a
  recolour.
- No token exists for the shared button style, the collapse-icon states, the layer
  active hairline/glow, the vignettes, or the card gradient reach.
- The UI Style panel has no tabs and no per-section grouping; it is a filtered list.
- Overlay clusters are plated. `BuildPreviewPanel` wraps each of seven clusters in an
  `SMixtormatGradientBox` painted `OverlayPlateTop`/`OverlayPlateBottom`
  (`Mixtormat_Preview.cpp:1390`, `:1402`, `:1459`, `:1469`, `:1482`, `:1494`, `:1505`) and
  rounds it by the global `CornerRadius`. The prototype's overlays carry no plate, no
  title and no drag (`README.md:84`), and the prototype's radius is zero everywhere but
  cards. Removing a plate is a layout change, not a restyle.
- **Overlay cluster width is derived, not authored:** `InspectorWidth * 0.5f` on **three**
  clusters (`Mixtormat_Preview.cpp:1397`, `:1489`, `:1512`).
- `falloff.js` samples 6 stops (`falloff.js:8`); `GradientSamplesPerSpan` is 12
  (`MixtormatDesignTokens.h:66`). The port needs the curve, not the stop count.
- `--well-saturation` / `--well-hover-saturation` are `backdrop-filter: saturate()`
  (`components.css:214`), not `filter: saturate()`. The other 21 are paint-layer
  filters. Slate has no backdrop filter, so those two need their own decision.
- `RecomputeDerived()` owns five values the prototype authors independently:
  `MenuItemHeight = ButtonHeight`, `MenuLipHeight = GroupHeaderHeight`,
  `TabHeight = ButtonHeight`, `SegmentHeight = RowHeight`,
  `FontSliderLabel = FontBody - 1`
  (`MixtormatDesignTokens.h:549-560`). Reaching `menu-row-height: 20px` next to
  `button-height: 24px`, or `control-label-size: 10px` next to `body-size: 10px`,
  means editing that function, not adding a token.
- Only `inline` tokens can be registered. `THEME_NUMBER` takes `&MixtormatTokens::Name`
  (`MixtormatLiveTheme.cpp:47-48`), so every `constexpr` token is deliberately absent
  from the registry. New tokens that the UI Style panel must edit have to be `inline`.
- `FMixtormatMutableStyleSet` overwrites values in place and never removes keys
  (`MixtormatMutableStyleSet.h:16-43`). Renaming a registered brush or widget style
  leaves the old allocation alive and unreferenced; changing a key's type trips
  `check(WidgetKeys[Name] == T::TypeName)` (`:35`).
- A theme edit is not a brush swap. `ApplyPendingTheme` captures layout state, empties
  `ChildSlot`, calls `FMixtormatStyle::Refresh()`, and rebuilds the workspace
  (`SMixtormat_Theme.cpp:146-166`).
- Every interactive state snaps. State is a binary branch in a paint-time attribute
  (`SMixtormatLayerRow.cpp:244-252`, `SMixtormatToggle.cpp:78-85`,
  `SMixtormatSegmentedControl.cpp:75-95`) or a discrete brush swap
  (`SMixtormatInspectorGroup.cpp:210-216`). `SMixtormatIconButton` tracks `bPressed`
  but never paints it, so press has no visual at all
  (`SMixtormatIconButton.cpp:71`, `:86`). See "State animations and interpolation".

## Complete token coverage

Every authored token in `tokens.css` must land somewhere in Unreal. Nothing is
carried over as "close enough". This is the checklist.

### Geometry: sizes, gaps, spacing, insets, thickness

| Prototype token group | Unreal destination |
| --- | --- |
| `row-height`, `row-gap`, `paired-gap`, `dropdown-label-gap`, `dragger-text-inset`, `dropdown-label-ratio` | existing `RowHeight`, `RowGap`, `RowLabelGap`, `DraggerTextInset`, `DropdownLabelRatio`. `paired-gap` needs a real token: `MakePair` hardcodes a spacer of `RowGap * 2.0f` (`SMixtormatRow.cpp:140`), which is 6px against the prototype's 3px. Add `PairedGap` and use it there |
| `icon-size`, `icon-hit-padding` | `IconButtonSize` for `icon-size`. `IconButtonHitSlop` is **not** 1:1 with `icon-hit-padding`: CSS adds 2.5px on *each* side, while `SMixtormatIconButton` computes `TargetSize = GlyphSize + IconButtonHitSlop` once (`SMixtormatIconButton.cpp:23-24`), so the equivalent total is 5px, not 2.5. Retune the value, not the token |
| `menu-icon-size` | existing `MenuIconSize` (`MixtormatDesignTokens.h:249`), which is what the menu item actually draws (`SMixtormatMenuItem.cpp:38-39`). Retune 12 to 14 |
| `thumbnail-size` | existing `LayerThumbnailSize` (`MixtormatDesignTokens.h:474`), read by the layer row thumbnail box (`SMixtormatLayerRow.cpp:130-131`). Retune 24 to 20; `gallery-tile-size` and `gallery-swatch-radius` stay **new** |
| `topbar-icon-size` | existing `ToolbarIconSize`, badly named but it is the token every top-bar button uses (`SMixtormat_Shell.cpp:116`, `:138`, `:161`, `:184`, `:220`, `:243`, `:266`). Retune 12 to 18; do not add a second top-bar icon token |
| `foldout-height`, `-gutter`, `-body-top/-bottom`, `-outer-top/-bottom`, `-header-padding-*` | **new** `Foldout*` tokens; `GroupHeaderHeight`/`PanelGutter` partially cover them today |
| `card-*` geometry (radius, header height, four header pads, four outer margins, body pads, header margins, leading gap) | **reuse the existing `GroupCard*` tokens, do not add a parallel family** — see "Naming collisions" below. Most properties already map: `card-header-*` to `GroupCardHeaderPadding*`, `card-outer-*` to `GroupCardOuterMargin*`, `card-body-horizontal` to `GroupCardHorizontalPadding`, `card-body-top/-bottom` to `GroupCardContentPadding*`, `card-header-height` to `GroupCardTitleHeight`, `card-leading-gap` to `GroupCardLeadingGap`. Only `card-radius`, `card-header-margin-top/-bottom`, `card-falloff-power`, `card-gradient-reach`, `card-header-saturation` and `card-body-saturation` are genuinely new roles |
| `group-button-*` geometry (`group-button-height`, `-hairline-width`, `-separator-width`, `-separator-height`) | **new** `GroupButton*`. `ButtonHeight` is the generic button height, not this family |
| `layer-height`, `child-height`, `layer-group-height`, `layer-gap`, `layer-indent`, `layer-badge-width` | existing `LayerRowHeight`, `LayerChildRowHeight`, `LayerGroupRowHeight`, `LayerRowGap`, `LayerChildIndent`, `BadgeWidth`. `BadgeWidth` is `constexpr` (`MixtormatDesignTokens.h:281`), so it is fixed until that changes |
| `layer-hierarchy-line-width`, `layer-visibility-size/-radius`, `layer-active-*` | **new**. `LayerConnectorOpacity` (`MixtormatDesignTokens.h:496`) already carries the connector opacity at 0.45. The **width is not a token problem**: connectors are PNG brushes (`TreeTee`/`TreeElbow`) drawn at `LayerChildIconSize` (`SMixtormatLayerChildRow.cpp:80-88`), so stroke weight is baked into the artwork and only the alpha is tunable. Exact parity needs a thickness-aware path (procedural draw or per-weight PNGs), not a new float |
| `well-border-width`, `well-radius` | **new**. `OutlineWidth` is one global stroke weight and `CornerRadius` is a global radius; the prototype's well is a 1px border on a 0px radius, which the globals cannot express together |
| `overlay-control-width` | **new**; overlay width must stop deriving from `InspectorWidth` |
| shell: `left-width`, `inspector-width`, `topbar-height`, `status-height`, `panel-padding`, `splitter-size/-hit-size` | existing `LayerStackWidth`, `InspectorWidth`, `TopBarHeight`, `StatusBarHeight`, `PanelPadding`, `SplitterHandleSize/HitSize` |
| popovers: `menu-width`, `menu-row-height`, `menu-padding`, `popup-lip-height`, `help-*` | `MenuWidth`, `MenuPanelPadding` map directly. `MenuItemHeight` and `MenuLipHeight` exist but are derived (see gaps), so `menu-row-height` and `popup-lip-height` need a `RecomputeDerived` change. `help-max-width`, `help-padding` and `help-delay` have **no** destination: the plugin has no hover-help surface, only editor `SToolTip`s |
| per-role icon sizes (`foldout-icon-size`, `card-icon-size`, `layer-icon-size`, `layer-module-icon-size`, `overlay-icon-size`, `rail-icon-size`, `icon-sheet-preview-size`) | **new**, one token each. `menu-icon-size`, `thumbnail-size` and `topbar-icon-size` already have destinations and are listed above. The prototype comment at `tokens.css:230-235` is explicit that equal values are not one role |
| `foldout-icon-padding`, `card-eye-size` | **new**. `ChevronSize` (`MixtormatDesignTokens.h:205`) and `LayerEyeSize` (`:493`) already exist and cover the glyph box, not the padding around it |
| `gallery-tile-size`, `gallery-gap`, `gallery-swatch-radius`, `thumbnail-radius` | **new**. The tile gallery has its own family (`MaterialGalleryTileDefault`/`Gap`/`Padding`, `MixtormatDesignTokens.h:307-313`) whose min/max/step grid the port must not disturb — retune the default only |
| `badge-width` | **new**. `BadgeWidth` (`MixtormatDesignTokens.h:281`) covers `layer-badge-width` |
| `window-grip-size` | **new** |
| dialogs/popovers: `dialog-width`, `dialog-padding`, `popover-width`, `popover-padding`, `popover-gap` | `DialogPadding` covers `dialog-padding`. The rest are **new**; `ActionDialogWidth` is the action dialog, not this one |
| `gallery-height`, `shell-gap` | **new**. Neither has a destination today; `gallery-height` was previously grouped with the covered shell tokens and should not have been |

### Colour and opacity

| Prototype token group | Unreal destination |
| --- | --- |
| base roles (`ground`, `text`, `accent`, `modified`, `warning`, `shade`, panel/header/hairline/layer/child channels) | existing `MixtormatPalette` roles; RGB-channel triples collapse into single `FLinearColor` roles, so `*-rgb` becomes one role and its separate `-opacity` becomes that role's `A` |
| `*-saturation` (23 tokens: foldout, hairline, card, layer, child, fill, well, group-button, layer-active-glow) | **new**, plus a saturation helper. `tokens.css:81-112`, `:202`, `:207`, `:317` |
| `zero-tick-opacity` | **new**. `Tick()` derives from `WellOutline` at a flat 0.4 (`MixtormatPalette.h:144-149`) |
| `surface-lift-opacity`, `hover-lift-opacity` | **new**. `GroupCardBackground` hardcodes 0.05 in the palette (`MixtormatPalette.h:47-50`) |
| `toggle-disabled-shade-top`/`-bottom` | **new** |
| popup opacities (`popup-tint-opacity`, `popup-border-opacity`, `popup-shadow-opacity`) | `MenuTint` bakes 0.10 (`MixtormatPalette.h:128`); the border and shadow opacities are **new** |
| icon opacities (`foldout-icon-*` rest/hover/pressed and their background opacities, `card-icon-opacity`, `layer-icon-opacity`, `overlay-icon-opacity`, `overlay-grip-opacity`, `menu-icon-opacity`, `toolbar-icon-opacity`, `topbar-icon-opacity`, `layer-module-icon-opacity`, `icon-off-opacity`) | **new**. `IconRest`/`IconHover` (`MixtormatPalette.h:159-162`) are opaque role colours, not opacities; `OverlayIconRestOpacity` exists as a single overlay value |
| foldout icon channels (`foldout-icon-rgb`, `foldout-icon-hover-rgb`, `foldout-icon-pressed-rgb`) | **new**. Hover and pressed are both the accent (82 123 137), which `Accent()`/`AccentBright()` already hold (`MixtormatPalette.h:137-138`); rest is `text-rgb`, not the near-white `IconRest` uses today |
| `layer-active-hairline-opacity`, `layer-active-glow-opacity`, `-reach` | **new** |
| `card-header-opacity`, `card-body-opacity` | `GroupCardHeaderOpacity` (1.0) and `GroupCardBodyOpacity` (0.01) exist (`MixtormatDesignTokens.h:338-339`); the body's authored value is 0.09, so the mapping is a retune, not a new token |
| foldout paint opacities (`foldout-hairline-opacity`, `foldout-accent-multiply-opacity`, `foldout-accent-hover-multiply-opacity`) | **new**. The accent multiply pair (`tokens.css:146-147`) is the soft-light substitute the blend-mode row refers to, and it has no Unreal counterpart at all |
| well fill shade (`well-shade-top`, `well-shade-bottom`) | **new**. `WellTop`/`WellBottom` (`MixtormatPalette.h:87-88`) are opaque hex, but the prototype shades the ground at 0.64 to 0.13, so the alpha has to become a token |
| two overlay plate systems (`overlay-plate-rgb`/`-opacity`, `overlay-top-rgb`/`overlay-bottom-rgb`/`overlay-ground-opacity`) | `overlay-plate-*` (21 22 24 at 0.85) is `OverlayButtonPlate` exactly (`MixtormatPalette.h:172`). `overlay-top`/`-bottom`/`-ground-opacity` is `OverlayPlateTop`/`Bottom` at `OverlayPlateOpacity()` 0.62 (`:78-80`). These are two different surfaces and must not be merged |
| `group-button-text-opacity` | **new**; part of the `GroupButtonText` style below |
| group-button gradient and hairline opacities (three gradient pairs, three hairline opacities) | **new**; folded into the `GroupButton*` family below |
| blend-mode names (`plus-lighter`, `multiply`, `soft-light`, `normal`) | **not tokens**. Resolved to explicit colour math at authoring time |
| well border opacities incl. per-endpoint top/bottom and hover variants | **new**; `WellOutline` currently multiplies a flat `0.4f` |
| `modified-stripe-width`, `modified-stripe-opacity` | `ModifiedStripeWidth` exists at 3px (`MixtormatDesignTokens.h:169`); the opacity is **new** |
| `fill-falloff-power`, `foldout-falloff-power`, `card-falloff-power` | **new** scalars. `falloff.js:17` reads them and emits stops; no Unreal token carries an exponent today |
| `fill-body-active-top`/`-bottom`, `fill-shade-mid`, `fill-shade-end` | `FillTopActive`/`FillBottomActive` (`MixtormatPalette.h:110-111`) and `MultiplyMid`/`MultiplyEnd` (`:123-124`) exist, but every one is a flat literal alpha with no ramp. The active pair is **new**; the shade endpoints need a curve, not a colour |
| layer/child horizontal opacities (`child-*-opacity`, `-left/-right`) | existing `LayerChild*` colors carry baked alpha; split into role + opacity so both are tunable |
| `*-vignette-*`, `foldout-title-opacity`, `card-title-opacity`, `control-label/value-opacity`, `text-muted/disabled-opacity` | **new** where absent |

### Typography

| Prototype token | Unreal destination |
| --- | --- |
| `body-size`, `caption-size`, `dragger-font-size`, `card-title-size`, `foldout-title-size`, `layer-group-title-size`, `group-button-font-size`, `control-label-size` | `body-size` is `FontBody`. `caption-size` is `FontCaption`, read by `Mixtormat.RowCaption` (`MixtormatStyle.cpp:620-628`) — same role, retune 8 to 11, do not add a token. `dragger-font-size` and `control-label-size` are **new**: CSS authors the dragger/value face and the control label face independently, and Unreal drives all three slider roles — label, value and entry — from the single `FontSliderLabel` (`MixtormatStyle.cpp:587`, `:596`, `:615`). Split them so both degrees of freedom survive. `foldout-title-size` and `group-button-font-size` are **new** |
| weights (`value-weight`, `card-title-weight`, `foldout-title-weight`, `group-button-font-weight`, `layer-group-title-weight`, `control-label-weight`) | existing bold switches (`CardTitleBold`, `GroupCardTitleBold`, `GroupHeaderBold`) are 0..1 resolved by a hard `>= 0.5 ? Bold : Regular` (`MixtormatStyle.cpp:50-54`); the prototype wants real weights (400/600), so **new** per-role weight tokens feeding `FSlateFontInfo`. Split label and value first: today only the value is bold (`:596`), and `--control-label-weight: 400` says the label should not be |
| tracking (`foldout-title-tracking`, `card-title-tracking`, `group-button-tracking`, `control-label-tracking`) | `card-title-tracking` already has the right counterpart: the non-compact card title reads `Mixtormat.GroupCardTitle` (`SMixtormatInspectorCard.cpp:49`), and that style is built from `GroupCardTitleLetterSpacing` (`MixtormatStyle.cpp:651`). Retune only. (`Mixtormat.CardTitle` at `:640` is the unrelated compact popover card and reads `CaptionLetterSpacing` — do not change it for this token.) Both Unreal values are in 1/1000 em, so the px authored values need a unit conversion. `foldout-title-tracking`, `group-button-tracking` and `control-label-tracking` have no existing entry and are **new** |
| `--font-family` (Roboto / Inter) | **not a numeric token**. Slate font faces come from the composite font; Roboto stays default, Inter is an opt-in swap |
| `dragger-label-case` | **prototype-only**. Slate has no per-row transform; either drop it or map to separate style entries |

### Naming collisions: reuse the existing names, add only what is missing

The `card-*` row above must not become a 1:1 rename of `GroupCard*`, and it must not
become a parallel `Card*` family either. The rule is narrower: **reuse a persisted
Unreal token wherever the semantics already match, and add only the roles that have no
home.** Every mapping below already has a home:

| Prototype | Existing Unreal token |
| --- | --- |
| `card-header-left/-top/-right/-bottom` | `GroupCardHeaderPaddingLeft/Top/Right/Bottom` |
| `card-outer-left/-top/-right/-bottom` | `GroupCardOuterMarginLeft/Top/Right/Bottom` |
| `card-body-horizontal` | `GroupCardHorizontalPadding` |
| `card-body-top/-bottom` | `GroupCardContentPaddingTop/Bottom` |
| `card-header-height` | `GroupCardTitleHeight` |
| `card-header-opacity` | `GroupCardHeaderOpacity` |
| `card-body-opacity` | `GroupCardBodyOpacity` |
| `card-leading-gap` | `GroupCardLeadingGap` |

Only `card-radius`, `card-header-margin-top/-bottom`, `card-falloff-power`,
`card-gradient-reach` and the two card saturations are new roles.

Three reasons this is the only safe shape:

1. **The token names are the persisted keys.** `THEME_NUMBER` stringifies its second
   argument as the registry name (`MixtormatLiveTheme.cpp:47-48`) and `Serialize` writes
   `Entry.Name` as the JSON key (`:261`). A rename orphans every saved
   `Saved/Mixtormat/LiveTheme.json`, and `Deserialize` rejects the whole document on one
   unknown numeric key (`:302-307`) — a user's theme stops loading, it does not
   half-load. Do not delete the old keys in this port.
2. **`Card*` is already taken, twice.** `CardPadding`, `CardGap` and `CardTitleGap`
   (`MixtormatDesignTokens.h:331-334`) are the compact popover card, deliberately kept
   apart from the inspector card (`:336-337`). `FontCardTitle` (`:518`) and
   `CardTitleBold` (`:535`) are that same compact card. So dropping the `Group` prefix
   from `FontGroupCardTitle` or `GroupCardTitleBold` collides directly, and the
   `GroupCard*` names are what keep the two cards distinguishable.
3. **The readers are already correct.** `SMixtormatInspectorCard.cpp` reads seventeen
   distinct `GroupCard*` tokens across `Construct` and `OnPaint` (`:37`, `:40`, `:56`,
   `:60-84`, `:90`, `:94`, `:111`, `:114`, `:185-192`, `:198-204`), and its title reads
   `Mixtormat.GroupCardTitle` (`:49`). `GroupCardLeadingIconSize` additionally reaches
   four inspector builders (`MixtormatInspectorHeight.cpp:65`, `:72`,
   `MixtormatInspectorIds.cpp:443`, `MixtormatInspectorLayer.cpp:180`);
   `MixtormatStyle.cpp:645-654` builds the title style; `MixtormatLiveTheme.cpp:82-102`
   registers twenty `GroupCard*` entries and `:192`, `:194` two colours. Renaming is all
   cost and no benefit here.

The six new roles take the `GroupCard` prefix so the family stays one family:
`GroupCardRadius`, `GroupCardHeaderMarginTop/Bottom`, `GroupCardFalloffPower`,
`GroupCardGradientReach`, `GroupCardHeaderSaturation`, `GroupCardBodySaturation`.

One caller is already wrong in the direction the port is heading.
`MakeFeaturePreviewButton` defaults its icon to `LayerEyeSize` (`Mixtormat.h:213`), a
layer-stack token, and its four card callers all override it with
`GroupCardLeadingIconSize` anyway. Give it a card default while the family is open.

### What must be built, not just registered

- A saturation helper applied per paint layer, with the caller supplying the value.
- An exact soft-light compositing helper, for the foldout accent tint and the layer-group
  cross pass. Neither is an add, so the existing plus-lighter precedent does not cover
  them.
- Radial/vignette support in `MixtormatGradientPainter` (currently linear only:
  `MakeGradient` takes an `EOrientation` and nothing else, `MixtormatGradientPainter.cpp:88-95`).
- Per-endpoint well border falloff instead of one flat multiplier
  (`MixtormatPalette.h:91-102`).
- The sampled power-curve falloff that `falloff.js` implements in six stops, plus a
  decision on the two backdrop-filter saturations.
- A shared `FButtonStyle` family plus a separator child brush.
- Five `RecomputeDerived()` changes: `MenuItemHeight`, `MenuLipHeight`, `TabHeight`,
  `SegmentHeight` and `FontSliderLabel` are currently locked to `ButtonHeight`,
  `GroupHeaderHeight`, `RowHeight` and `FontBody`
  (`MixtormatDesignTokens.h:549-560`). Every one of them blocks a token above.

## Work the tables imply but the plan previously omitted

- **Theme edits rebuild the workspace, they do not repaint it.** `ApplyPendingTheme`
  transfers layout state out, nulls `ChildSlot`, calls `FMixtormatStyle::Refresh()` and
  calls `BuildWorkspaceUI()` (`SMixtormat_Theme.cpp:146-166`). A new style entry is only
  visible if `Refresh()` registers it, and a change that moves geometry is judged after a
  full layout round trip with scroll, group expansion and page index restored. Budget for
  that in every screenshot step rather than assuming a brush swap.
- **`Refresh()` order matters and is already correct.** `Refresh()` calls
  `RecomputeDerived()` before it reads any token (`MixtormatStyle.cpp:73-74`), so a new
  derived value is correct by the time the first brush is built. Keep it first: any new
  per-state brush that reads a derived value must be registered after it, not before.
- **`SetPngIcon` re-registers on every `Refresh()`, but named brushes are only overwritten.**
  The icon path calls `SetContentRoot` and re-runs `SetPngIcon` each pass
  (`MixtormatStyle.cpp:118`, `:770-771`), so a new chevron state is safe to add there.
  A new colour brush added as a `new FSlateRoundedBoxBrush` allocation is also safe,
  because `FMixtormatMutableStyleSet::Set` copies over the existing allocation and
  deletes the incoming one (`:16-28`). What is *not* safe is reading `bFirstRegistration`
  for anything: it only distinguishes the very first pass (`:75`, `:109-112`).
- **Not every state is a registered brush.** Layer rows read per-state colours through
  `SMixtormatGradientBox` lambdas (`SMixtormatLayerRow.cpp:65-69`), not through named
  brushes. A colour reached through those lambdas is resolved per paint, so it takes a
  saturation value per call site; a `Refresh()`-only change cannot reach a state whose
  colour is computed in the widget.
- **Drag/drop targeting is fraction-normalised, not pixel-sized.** `LocalFraction` divides
  by the row's own height (`SMixtormatDropTargets.h:92-102`), so changing
  `LayerRowHeight`, `LayerChildRowHeight` or `LayerRowGap` does not break the
  comparisons. `GroupRowIntoZoneFraction` is 0.25 (`:123-132`), which reads as
  **25% Before + 50% Into + 25% After**, not a 25% "into" band: the Into zone is
  `Fraction > 0.25 && Fraction <= 0.75`. At 22px that is 11px, and at the prototype's
  18px group row it is 9px. Verify the 9px band is still hittable; if not, raise the
  fraction rather than the row height.
- **Drop-zone geometry is not the only thing coupled to row metrics.** The insertion
  line is `DropInsertionLineThickness` (a `constexpr`, `MixtormatDesignTokens.h:90`)
  and the child indent is folded into the row's own padding as
  `LayerRowInsetLeading + LayerChildIndent` (`SMixtormatLayerChildRow.cpp:67-71`).
  Moving `layer-indent` therefore moves the child text without moving the drop target,
  which is correct but means a screenshot comparison has to check the connector
  alignment as well as the targeting.
- **`LayerGroupRowHeight` is not in the live theme registry**
  (`MixtormatLiveTheme.cpp:141-144` registers the other two). `LayerEyeSize` and
  `ChevronSize` are (`:147-148`). Register what the panel must edit; leave the rest.
- **`Mixtormat.Icon.ChevronRight` / `ChevronDown` already exist**
  (`MixtormatStyle.cpp:770-771`, reachable via `MixtormatIcons::ChevronDown()` and
  `ChevronRight()`, `MixtormatIcons.cpp:56-57`). Step 1 adds the state styling and the
  swap, not the registration.
- **Removing the overlay plates is a layout change in `BuildPreviewPanel`.** Seven
  clusters are `SMixtormatGradientBox` with their own `.Padding`
  (`Mixtormat_Preview.cpp:1390`, `:1402`, `:1459`, `:1469`, `:1482`, `:1494`, `:1505`);
  dropping the plate also drops the cluster inset that was measured against it, and each
  one rounds by the global `CornerRadius`. There is **no** Slate `.overlay-title`
  wrapper to remove — the prototype's title strip has no counterpart here, so nothing
  needs deleting for it. Keep every control, gizmo, combo and action: only the plate and
  its inset go. Three clusters derive their width from `InspectorWidth * 0.5f`
  (`:1397`, `:1489`, `:1512`) and those are the ones `overlay-control-width` replaces.
- **The card painter has no reach hook to extend.** `SMixtormatInspectorCard::OnPaint`
  builds three stops from the measured header height
  (`SMixtormatInspectorCard.cpp:193-208`), so reach has to be added to that array rather
  than tuned. `HeaderEnd` is already derived from `HeaderBox`'s cached geometry, which is
  the same seam `falloff.js:47-48` computes.
- **`--card-gradient-reach: 0px` means the mirrored tail is dead on arrival.**
  `falloff.js:62` only emits the body/bottom stop pair when `reach > 0`. Port the seam
  maths so a future non-zero reach works, but do not build the mirror for zero.

## Translation rules

1. One authored source per role. `MixtormatDesignTokens.h` for geometry,
   `MixtormatPalette.h` for colour, `MixtormatLiveTheme.cpp` for the editable
   registry. `tokens.css` stays the prototype's reference, not a second engine of record.
2. Add the exact same names the prototype uses, so a future diff is mechanical.
   `--foldout-icon-opacity` becomes `MixtormatTokens::FoldoutIconOpacity`. Three
   exceptions, all deliberate: `*-rgb` triples collapse into one palette role, the
   `*-blend-mode` names are resolved at authoring time rather than stored, and
   `--font-family` is not a numeric token.
3. Every new token is registered in `MixtormatLiveTheme` with a min, a max, a
   default, and a category matching the prototype's UI Style section. Two
   constraints the registry imposes: the token must be `inline`, not `constexpr`
   (`MixtormatLiveTheme.cpp:47-48`), and a token derived in `RecomputeDerived()`
   must not be registered, because `Initialize` captures defaults before it runs
   (`MixtormatLiveTheme.cpp:37-43`). Saturation tokens need a range that brackets 0
   to roughly 3, not 0 to 1.
4. Additive means source colour added at alpha. Multiply means a darker source
   painted over the same ground. Never rely on a browser blend mode name.
5. Geometry that a browser derives (`calc()`, `%`, `min()`) is computed in C++ and
   passed to Slate explicitly.
6. Every visual change is compared in-engine against the prototype. The browser is a
   reference, not an oracle.

## Mapping of the shared button style

The prototype unifies tabs, segmented groups, and toolbar actions into one token set
(`group-button-*`). In Slate that is one **token** family feeding three different
widget-style types, because the three callers are not the same kind of widget:

- top-bar and toolbar actions are `SButton` — an `FButtonStyle`
  (`Mixtormat.TopButton`, registered at `MixtormatStyle.cpp:235-247`);
- tabs are an `SCheckBox` reading `Mixtormat.TabToggle`, swapping a named brush per
  state (`SMixtormatTabStrip.cpp:52-58`) — an `FCheckBoxStyle`;
- segmented control cells are hand-painted, not styled: an `SMixtormatGradientBox` with
  its own start/end/multiply colours and a `CornerRadiusInner`
  (`SMixtormatSegmentedControl.cpp:33-49`).

So the work is: add the tokens once, then add an `FButtonStyle` and an `FCheckBoxStyle`
that read them, and feed the existing custom segment painter from the same tokens.
Do not build a five-entry `FButtonStyle` family and try to make `SCheckBox` and the
segment painter use it.

- `Mixtormat.GroupButton` (`FButtonStyle`) and `Mixtormat.GroupButtonSelected`
  (`FCheckBoxStyle`), each carrying normal / hovered / pressed / disabled.
- Text style `Mixtormat.GroupButtonText` from `group-button-font-size`, `-weight`,
  `-tracking`, `-text-opacity`. The segment painter reuses it via
  `ColorAndOpacity` rather than a text style, so it must be readable as a bare
  `FLinearColor` too.
- Top hairline drawn into the normal brush; separators drawn as a 1px child border
  between cells, matching `--group-button-separator-*`.

Existing `Mixtormat.TopButton` and the tab styles keep their current names; the new
entries are added alongside so nothing regresses.

## UI Style panel: current state

It already ships, and it is not a port target — it is the engine-side counterpart:

- `SMixtormatLiveThemePanel` renders every `FMixtormatLiveTheme` entry as a
  label / spin box / reset row, plus one row per palette colour
  (`SMixtormatLiveThemePanel.cpp:38-118`). Numeric labels read
  `"Category / Name"`; colour labels read `"Colors / Name"`. Rows are built once in
  `Construct` and filtered by a per-row `Visibility_Lambda`, not re-sorted on search.
- Colours are `SColorBlock` inside a button that opens `SColorPicker`
  (`:99-101`, `:191-207`), so a colour row has no inline swatch-plus-picker
  equivalent in the prototype; the prototype's picker is a browser control.
- Opened by `UI STYLE` in the top bar (`SMixtormat_Shell.cpp::BuildTopBar`, `:199-205`),
  which is visible only when `EnumerateShippedSourceDirectories()` is non-empty
  (`SMixtormat_Shell.cpp:58-59`). That is a shipped-source check, not
  `bShippedWithDeveloperSources`.
- `SMixtormat_Theme.cpp::OpenLiveThemePanel` hosts it in a 620x720 `SWindow`
  (`:109-113`) and reuses an existing window if one is open (`:104-108`).
- Edits call `OnThemeChanged` -> `RequestThemeRefresh`, which defers to a 0.1s active
  timer and then rebuilds the workspace (`SMixtormat_Theme.cpp:128-135`, `:137-168`),
  so changes repaint live. `ChangeNumber` is gated on `CanEdit` (`:171`) but
  `ChangeColor` is not (`:180`) — the surrounding `SBorder.IsEnabled(CanEdit)` is
  the only thing stopping an edit during a bake. Save/Load go to
  `Saved/Mixtormat/LiveTheme.json` (`MixtormatPaths.cpp:222-225`; `ProductName` is
  the literal `Mixtormat`); `Save` writes a `.tmp` and moves it into place
  (`MixtormatLiveTheme.cpp:349-364`), and `Load` fails hard if the file is absent
  rather than falling back to defaults. Reset restores authored defaults without
  touching the saved file (`SMixtormatLiveThemePanel.cpp:231-237`).

What it does **not** yet have, and what the port still owes it:

| Prototype UI Style | Unreal today | Needed |
| --- | --- | --- |
| Tabs per section | one flat scrollable list, filtered by a search box | tab strip + per-section visibility |
| Two-column layout | one row per token, full-width label plus a 100px spin box (`:56`, `:93`) | two-column wrap for numerics |
| Section ordering | registry order in `MixtormatLiveTheme.cpp`, which is roughly alphabetical by variable, not the prototype's section order | group tokens under the prototype's section names |
| Saturation / falloff / reach tokens | not registered | register with min/max, then wire into paint code |
| Hover help with defaults | colour rows carry `ToolTipText(FromName(Name))`; **numeric rows carry no tooltip at all** (`SMixtormatLiveThemePanel.cpp:56-64` vs `:96`) | show default and range on both |
| Panel layout follows the token system | hardcoded `PanelPadding = 10.0f`, `RowGap = 3.0f`, `ControlWidth = 100.0f` in an anonymous namespace (`:24-27`), shadowing `MixtormatTokens::PanelPadding` and `RowGap` | reuse the shared tokens so the panel restyles with the rest of the UI |
| Search covers categories | `Matches` tests name and category (`:164-167`), but colour rows only match on name and always report the literal category `"Colors"` (`:83-90`) | index colours under real categories if the tabs land |

Panel work is therefore step 13 below, and it should follow the token registrations:
a control for a token that does not exist yet would be dead UI.

## State animations and interpolation

Every state in the plugin today snaps. State is read as a binary branch in a paint-time
attribute — `IsHovered() ? LayerHoverTop() : Panel()`
(`SMixtormatLayerRow.cpp:244-252`), `IsHovered() ? FillTopHover() : FillTop()`
(`SMixtormatToggle.cpp:78-85`) — or as a discrete brush swap,
`IsHovered() ? "Mixtormat.HeaderHairlineGlow" : "Mixtormat.HeaderHairline"`
(`SMixtormatInspectorGroup.cpp:210-216`, `SMixtormatLayerRow.cpp:267-271`). Nothing
interpolates, and `SMixtormatIconButton` tracks `bPressed` for click cancellation
(`SMixtormatIconButton.cpp:71`, `:86`) but never paints it, so press is invisible.

### What the prototype actually requires

The prototype authors **no transitions at all**. There is no `transition`, `animation`
or `@keyframes` declaration in `components.css`, `tokens.css`, `popovers.css` or
`fonts.css`, and no `requestAnimationFrame` in any script. Hover, pressed, selected and
open states are instantaneous in CSS.

The only timing value in the whole contract is `--help-delay: 350ms`
(`tokens.css:341`), and it is a **delay before the help appears**, not a transition: it
is consumed by a `setTimeout` in `scheduleHelp` (`popovers.js:63-73`), with a hardcoded
350ms fallback. `help-max-width` and `help-padding` are geometry.

Therefore:

- **Zero animation is required for CSS parity.** Every transition below is either
  optional Unreal polish or, at most, parity with the one delay the prototype does
  author.
- `--help-delay` maps to the plugin's existing tooltip delay behaviour, which is an
  editor `SToolTip` the plugin does not own. It is a behaviour port, not a timing token.
- Adding animation must not be allowed to change any static comparison. Every state
  still has to be validated in its final static form first.

### Parity vs polish

| Transition | Status | Note |
| --- | --- | --- |
| Hover help delay (`--help-delay`) | **parity** | The only authored timing. Belongs to tooltip behaviour, not to a motion system |
| Everything else in this section | **polish** | Slate snaps today and the prototype snaps too; adding motion is a deliberate departure |

This is stated plainly because the risk is that a motion pass is mistaken for parity
work and used to paper over a static mismatch. It is not.

### Timing tokens

The prototype authors no durations and no easing curves, so the whole timing set is
Unreal-only polish. Keep it small:

| Token | Suggested default | Scope |
| --- | --- | --- |
| `HoverTransitionDuration` | 120ms | rest to hover and back, on every family |
| `PressTransitionDuration` | 60ms | hover to pressed and back |
| `SelectionTransitionDuration` | 100ms | rest/hover to selected and back |
| `ExpandTransitionDuration` | 140ms | foldout chevron and tint only (see below) |
| `PopupFadeDuration` | 100ms | optional entrance/exit for menus and help |
| `HoverEaseExponent` | 1.0 | power curve, same idiom as `falloff.js` |

Six values, not dozens. `HoverEaseExponent` deliberately reuses the existing power-curve
vocabulary (`falloff.js:17`) instead of introducing a separate easing concept the rest
of the plan does not already speak.

These should be `inline float` and registered in `MixtormatLiveTheme` under a single
**Motion** category, so the UI Style panel exposes them in one place and clearly
separates them from the CSS-derived set. They are `constexpr`-free for the usual
reason: `THEME_NUMBER` takes `&MixtormatTokens::Name`
(`MixtormatLiveTheme.cpp:47-48`).

### Recommended architecture: a shared state helper, not `FCurveSequence` alone

`FCurveSequence` is the right primitive for the **easing shape**, and the wrong
primitive for the **state machine**. A curve sequence is evaluated at a time you supply;
it has no notion of a target, so reversing a transition means either re-keying the curve
at the current time or tracking the current value outside it. Re-keying on every
interruption is exactly the code that produces jumps.

The helper holds current and target per channel and borrows a curve only for the ramp:

```
struct FMixtormatStateChannel
{
    float Current = 0.0f;   // 0..1, the interpolated value painters read
    float Target  = 0.0f;   // 0 or 1
};

struct FMixtormatStateAnim
{
    FMixtormatStateChannel Hover, Press, Selection, Open;
    TSharedPtr<FActiveTimerHandle> Timer;

    void SetHoverTarget(bool bOn);   // and Press/Selection/Open
    float HoverT() const { return Hover.Current; }
};
```

A `FCurveHandle` (or an `FCurveSequence` of one 0..1 span) supplies
`Ease(elapsed / duration)` each tick. `Current` moves toward `Target` by evaluating the
curve over the remaining fraction, so an interruption mid-flight reverses from wherever
the value currently is. That handles every reversal case in the list below with no
special-casing, and it is why the helper must own `Current`/`Target` rather than
delegating to the curve.

`Disabled` is not a channel. It is a fixed state that suppresses the others, matching
how the widget already branches on `ShouldBeEnabled` first
(`SMixtormatIconButton.cpp:48-51`, `SMixtormatSlider.cpp:409`).

### Reverse and interrupted transitions

Required behaviour, all of which fall out of the Current/Target model:

| Case | Behaviour |
| --- | --- |
| Cursor leaves before hover-in finishes | `Target = 0`, ramp continues from current `HoverT`. No snap to 0 |
| Press begins mid hover-in | `Press.Target = 1`; `Hover` keeps its own value. Channels are independent, not a single enum |
| Press released off the glyph | `Press.Target = 0`. The visual must follow the same rule the click already uses — release outside the glyph cancels (`SMixtormatIconButton.cpp:91`) — otherwise the button looks cancelled but still fires |
| Selection changes while hovered | `Selection` moves on its own curve; `HoverT` is unaffected. The paint blends both |
| Foldout reverses while opening | `Open.Target` flips; the chevron and tint ramp back from current |
| Theme edit while a state is animating | Workspace rebuild destroys the widgets (`SMixtormat_Theme.cpp:146-166`). New widgets start at their target, so nothing dangles |

The widget tree must be destroyed cleanly on rebuild. Any registered active timer is
held by `TSharedPtr` on the widget, so a destroyed widget drops its timer with it — but
this only holds if no timer is registered against a parent that outlives the rebuild.

### What each state family interpolates

Paint-only unless noted. "Geometry" is called out where it is deliberately excluded.

| Family | Interpolates | Excluded |
| --- | --- | --- |
| Button rest/hover/pressed/selected | RGB, alpha, gradient stop values, saturation, hairline intensity | Any dimension |
| Icon rest/hover/pressed | Glyph RGB and alpha, background plate alpha | Glyph size — `SMixtormatIconButton` fixes the glyph box at Construct (`:33-35`); animating it would relayout |
| Foldout header hover | Tint RGB, tint alpha, saturation | Header height |
| Foldout hairline | Hairline intensity / glow alpha | Hairline thickness |
| Foldout accent tint | Multiply strength and the soft-light inputs | The tint's own geometry |
| Disclosure chevron | Rotation about its centre | Layout slot |
| Group Card hover/actions | Background alpha, header/body opacity, radius if stepped | Card margins |
| Slider / well rest/hover/active/disabled | Fill gradient stops, shade alpha, saturation | Fill width — that is the value being dragged |
| Well border endpoints | Per-endpoint alpha (the `well-border-top-opacity` / `-bottom` pair) | Border width |
| Saturation between states | Per paint layer, lerped between the two authored values | Never hoisted into the palette |
| Layer rest/hover/selected | Gradient start/end RGB, saturation, hairline | Row height and indent |
| Layer active hairline/glow | Glow alpha, glow reach | Glow is a paint, not a layout change |
| Child row hover/selected | Horizontal gradient endpoints | Indent |
| Group row hover/selected | Cross-axis lift, saturation | Group row height |
| Visibility squircle | Fill alpha, border colour | Squircle size |
| Tabs | Brush tint / check-box state colours | Tab width |
| Segmented controls | Cell gradient stops, text colour | Cell height |
| Top bar / group buttons | Plate RGB, gradient stops, text opacity | Button height |
| Preview rail buttons | Accent add amount, glyph opacity | Button size |
| Menus / popovers | Optional opacity fade | Menu width and row height |
| Hover help | Entrance/exit opacity only, if built at all | Help geometry |
| Vignettes | Optional opacity, when state-driven | Vignette radius and start |

Two structural constraints carry across all of these:

- **Saturation stays per paint layer.** An animated saturation is the caller's argument,
  lerped between two authored values, never a property of the palette role. See the
  audit note on one accent being used at 0.7, 1.5 and 1.5.
- **Soft-light stays explicit compositing.** Animation changes the strength and the
  inputs; it does not turn soft-light into an add.

### Slider needs a structural change first

`SMixtormatSlider::OnPaint` selects a **brush name** by state
(`SMixtormatSlider.cpp:408-416`), so the fill cannot interpolate: there is no colour to
lerp between, only four pre-baked brushes. The static port must move the fill to
hand-painted gradient stops fed by attributes — which it needs anyway for the fill ramps
and falloff in the surface steps — before any fill transition is possible. This is the
one place where the static port and the animation port genuinely collide.

### Expand/collapse: animate the indicator, not the height

`SMixtormatInspectorGroup` toggles body visibility through
`Visibility_Lambda` returning `Visible` or `Collapsed`
(`SMixtormatInspectorGroup.cpp:254-255`). `Collapsed` removes the body from layout
entirely, so there is no height to animate without a clipped-reveal implementation that
would relayout every frame.

Recommend the lower-risk option: **animate the chevron rotation and the header tint, and
leave body visibility immediate.** Nothing in the prototype asks for an animated height
— `<details>` in CSS opens instantly here, since there is no transition. If a reveal is
wanted later, it belongs in its own step and must not change the expansion state, the
scroll position or the layout-transfer behaviour in `ApplyPendingTheme`.

The chevron is currently a binary brush swap, `bExpanded ? ChevronDown() :
ChevronRight()` (`SMixtormatInspectorGroup.cpp:99-101`). Rotating needs a painted
rotation or two states cross-faded, since the PNGs are fixed artwork.

### Performance and lifecycle

Hard constraints:

- **No permanent ticking.** An active timer is registered on a state *change* and
  unregistered when every channel reaches its target. Nothing runs at rest.
- **Layer stack in particular.** Many rows exist at once, but only rows whose state is
  mid-transition should hold a timer. A fast sweep across a 100-row stack leaves a
  handful running briefly, not 100.
- **No `FMixtormatStyle::Refresh()` per frame.** A live-theme edit rebuilds the
  workspace; an animation frame must never do that. The style set stays static during
  motion.
- **No per-frame brush allocation.** Gradient brushes cannot be animated through the
  style set at all — that is why `MixtormatGradient::Paint` exists. Note that it
  currently heap-allocates a `TArray<FSlateGradientStop>` sized to the sampled spans on
  **every** paint (`MixtormatGradientPainter.cpp:71-72`). At
  `GradientSamplesPerSpan = 12` (`:69`) that is a per-frame allocation per animating
  surface. The animation pass should move that to an inline allocator or a reusable
  buffer; `SMixtormatGradientBox` already uses `TInlineAllocator<3>` for its multiply
  stops (`SMixtormatGradientBox.cpp:67`), which is the pattern to follow.
- **No widget-tree rebuild during a transition.** Animation lives inside an existing
  painter.
- **No material or viewport work.** Slate invalidation must not reach
  `SMixtormatPreviewViewport`, whose compose path has its own active timer
  (`SMixtormatPreviewViewport.cpp:596-598`). Paint-only invalidation keeps them separate.
- **No writes to the live theme.** Animated intermediate values are widget-local. The
  live theme holds authored targets only.

### Ownership

Three layers, and they must not blur:

| Layer | Holds | Changes when |
| --- | --- | --- |
| `MixtormatDesignTokens` / `MixtormatLiveTheme` | Authored **target** values for every state | A theme edit, a load, a reset |
| `MixtormatStyle` / `MixtormatPalette` | Static style resources and semantic colours | `Refresh()`, i.e. the same theme events |
| Widget animation state | The **current interpolation** between those targets | Every frame of a running transition |

A live-theme edit during a transition rebuilds the workspace, so widgets and their
`Current` values are recreated at their targets. That is the correct outcome and needs no
special handling beyond not leaking the timer.

### Accessibility and behaviour

Animation must not change click targets, drag/drop zones, transactions, selection
behaviour, popup behaviour, expansion state, keyboard focus, tooltips or data bindings.
In particular the 50%-of-row Into drop zone (`SMixtormatDropTargets.h:123-132`) is a
fraction of row height and is unaffected by paint-only motion, because no animated
property participates in layout.

A reduced-motion path is required. The helper must be able to snap straight to target —
set `Current = Target`, skip the timer — rather than each widget growing a second
implementation. The project already has a settings surface (`OpenSettings`,
`SMixtormat.h:178`), so a single toggle there is the natural home; the fallback when
disabled is that every animated family behaves exactly as it does today.

## Work order

Each step is independently reviewable and stops at a screenshot comparison. The order
is foundation first, then rendering primitives, then the surfaces that use them.

### Foundation

1. **Tokens and mappings.** Preserve every persisted `GroupCard*` key and reuse it (see
   "Naming collisions"). Add the genuinely new roles. Retune the ones that already have a
   home: `MenuIconSize` 12 to 14, `LayerThumbnailSize` 24 to 20, `ToolbarIconSize` 12 to
   18, `FontCaption` 8 to 11, `IconButtonHitSlop` 8 to 5. Add `PairedGap` and use it in
   `MakePair` in place of `RowGap * 2.0f` (`SMixtormatRow.cpp:140`). Convert the
   `constexpr` tokens the panel must edit to `inline` — `THEME_NUMBER` takes
   `&MixtormatTokens::Name` (`MixtormatLiveTheme.cpp:47-48`), so `BadgeWidth`,
   `OutlineWidth` and the like cannot be registered until they change.
2. **`RecomputeDerived()` unlock.** `MenuItemHeight`, `MenuLipHeight`, `TabHeight`,
   `SegmentHeight` and `FontSliderLabel` are all derived
   (`MixtormatDesignTokens.h:549-560`). Nothing reaches its authored value until this
   lands, and steps 3 to 11 all depend on it. Keep it separate so a layout regression is
   not confused with a paint one.

### Rendering primitives

3. **Paint-layer primitives.** A `Ground` palette role; per-paint-layer saturation (not
   palette read time — one accent is used at several saturations); an exact soft-light
   compositing helper; power-curve stop sampling; and a radial vignette path in
   `MixtormatGradientPainter`. Everything below is built on these, so a bug here shows up
   in every later screenshot.

### Surfaces

These steps are **static parity only**. Every state is validated here in its final
static form, with no motion, before any animation work begins.

4. **Controls.** Well fill shade and per-endpoint border falloff; slider fill, shade and
   falloff ramps; the modified stripe; the zero tick; toggle disabled shade; paired
   spacing. All of these are hand-painted today, so none is a brush swap. The slider fill
   must become attribute-fed gradient stops here, not in the motion phase: `OnPaint`
   currently picks a brush *name* by state (`SMixtormatSlider.cpp:408-416`) and cannot
   interpolate until that changes.
5. **Foldouts.** Geometry, title styling, the lift falloff, the soft-light tint layer,
   the hairline, the vignette, and the chevron states. Note `ChevronSize` is shared with
   the chip, the menu item, the layer group row and the child-output preview
   (`SMixtormatLayerGroupRow.cpp:166`, `SMixtormatLayerRow.cpp:216`,
   `SMixtormatChip.cpp:54`, `SMixtormatMenuItem.cpp:81`), and the preview uses
   `ChevronSize * 0.6f` (`SMixtormat_Preview.cpp:759-761`), so retuning it moves five
   callers — `FoldoutIconSize` is a separate token for exactly that reason.
6. **Group Cards.** Retune the existing `GroupCard*`; add only radius, header margins,
   falloff, reach, vignette and the two saturations.
7. **Shared action tokens.** An `SButton` adapter (`FButtonStyle`), an `SCheckBox`
   adapter (`FCheckBoxStyle`), and the existing custom segment painter fed from the same
   tokens. Three widget types, one token family.
8. **Layer stack.** Heights and indent, separate badge widths, thumbnails, squircle
   visibility, active glow, saturation, and thickness-aware hierarchy connectors (PNG
   brushes cannot carry stroke weight — `SMixtormatLayerChildRow.cpp:80-88`). Verify the
   9px Into drop band still feels right at the prototype's 18px group row.
9. **Shell and galleries.** Widths, splitter sizing, shell gap, panel vignette, gallery
   defaults. Preserve splitter resizing and the gallery zoom min/max/step grid.
10. **Menus, dropdowns, popovers and help.** Keep existing popup behaviour; unlock the
    menu row height and lip; port the popup surface; port help geometry and delay.
11. **Preview overlays.** Remove seven plates and their insets, replace all three
    `InspectorWidth * 0.5f` widths. Keep every control, gizmo and action.
12. **Typography.** Correct the existing mappings first (`FontCaption`, the
    `GroupCardTitleLetterSpacing` path), then split the slider label and value faces,
    then real weight faces, then px to 1/1000-em tracking, then the font-family choice.
13. **UI Style panel.** Tabs and real categories, two-column layout, default/range help,
    shared spacing.
14. **Parity validation.** Screenshots plus Save to Load, rebuild state restoration,
    splitters, gallery zoom, drag and drop, and open popups. This is the gate: the
    static port is not finished until every state reads correctly without motion.

### Motion

Entirely optional. Nothing here is required for CSS parity, because the prototype
authors no transitions at all.

15. **Shared state-animation primitive.** The current/target helper plus the six timing
    tokens, registered under a Motion category. Fix the per-paint allocation in
    `MixtormatGradient::Paint` (`MixtormatGradientPainter.cpp:71-72`) before anything
    animates through it, or every animating gradient allocates per frame. Add the
    reduced-motion toggle at the same time, so no family has to be written twice.
16. **Simple families first.** Icon button, toggle, tabs, segmented cells and top-bar
    buttons. These are the cheapest and validate the primitive end to end. Icon press
    becomes visible here for the first time (`SMixtormatIconButton.cpp:71`).
17. **Rows.** Layer, child and group rows. Largest count, so the timer-lifecycle and
    allocation work from step 15 gets its real test. Only mid-transition rows hold a
    timer.
18. **Foldout indicator.** Chevron rotation and header tint. Body visibility stays
    immediate; no height animation.
19. **Motion validation.** The list in "Validation", plus a frame-cost check with a full
    layer stack.

### Structure during the static port that makes motion cheap later

These are not extra work; they are choices in steps 4 to 11 that avoid a rewrite:

- Paint state through **attribute bindings**, not branch-and-return, so an animated
  value can flow into the existing `StartColor`/`EndColor`/`ColorAndOpacity` bindings.
  Every affected widget already uses them (`SMixtormatLayerRow.cpp:65-67`,
  `SMixtormatToggle.cpp:53-55`, `SMixtormatSegmentedControl.cpp:36-38`).
- Keep a rest value and a state value **distinct** rather than collapsing to one
  "current" colour, so a lerp has two endpoints.
- Resolve both endpoints per paint from tokens, not from a pre-baked brush, so a
  live-theme edit moves the endpoints without touching widget code.
- Avoid animating anything that feeds `ComputeDesiredSize`.

### Notes on ordering

- The saturation pass is per paint layer, not a palette-wide transform, so it cannot be
  a single late sweep the way it first appeared here. Each surface adopts its own
  saturation tokens in its own step; only the shared helper lands in step 3.
- Step 2 gates most of the surface steps. It cannot move past them.
- Steps 12 and 13 are wide but shallow; doing them late keeps the earlier visual
  comparisons attributable to a single change.

## Risk notes

- Every step above touches a style that other widgets read. Land one token group at a
  time and refresh the whole style set before judging. A refresh is a workspace
  rebuild, not a repaint (`SMixtormat_Theme.cpp:146-166`), so a geometry change is
  judged after layout state has been restored, not immediately.
- Registering a brush is a one-way door in `FMixtormatMutableStyleSet` (`:16-43`): keys
  are never removed, and reusing a name with a different style type asserts. Pick new
  style names once.
- Radius went to zero everywhere except cards in the prototype. `CornerRadius` is
  global in the plugin and is read by the overlay plates as well as the panels; the port
  must use per-surface radii and leave the global fallback alone until every caller is
  checked.
- Layer row heights, indents, and connector geometry feed drag/drop, but the drop zones
  are fraction-normalised (`SMixtormatDropTargets.h:92-132`), so height and gap changes
  are safe. The Into zone is 50% of the row, which shrinks from 11px to 9px when
  `LayerGroupRowHeight` goes 22 to the prototype's 18.
- Preview overlays are built once in `BuildPreviewPanel`. Removing their plates is a
  layout change there, not a style change, and it takes the cluster inset with it. Keep
  the individual control labels: the prototype drops only the cluster title strip, which
  has no Slate counterpart here.
- The prototype's variable fonts have not been validated in Slate. Use static weight
  instances for the engine and keep Roboto as the default family. Reaching the authored
  400/600 pair needs typefaces the default family may not carry: the three bold switches
  (`CardTitleBold`, `GroupCardTitleBold`, `GroupHeaderBold`) resolve through `Weight()`,
  a hard `>= 0.5 ? Bold : Regular` (`MixtormatStyle.cpp:50-54`, read at `:179`, `:633`,
  `:645`), so a real weight token needs a real typeface, not a switch.
- **Motion is optional and separable.** Nothing in the motion phase gates static parity.
  If it slips or regresses it can be dropped wholesale without touching the visual
  contract, because every animated family keeps a correct static form underneath.
- **A leaked active timer is the main lifecycle risk.** The helper holds its handle as a
  `TSharedPtr` on the widget, so a destroyed widget drops it — but that only holds while
  no timer is registered against a parent that outlives the workspace rebuild
  (`SMixtormat_Theme.cpp:146-166`). Register timers on the widget that owns the state.
- **Animation must not become the reason a static mismatch survives.** Every state is
  signed off static first. A transition that makes a wrong value look intentional is a
  regression, not a fix.
- **Per-frame gradient allocation.** `MixtormatGradient::Paint` allocates on every paint
  (`MixtormatGradientPainter.cpp:71-72`). Safe at rest, a real cost once a layer stack
  animates. Fixed in step 15, before any motion.

## Validation

Static review plus in-engine screenshots for each step. No build, test, or shell
command was run to produce this plan.

### Static parity

As listed in work order step 14.

### Motion

Run only for the motion steps. Every case below should pass both with animation enabled
and with the reduced-motion path, where the expected result is an immediate snap:

| Case | What to check |
| --- | --- |
| Hover in and out | Reaches the authored value; no snap at the end |
| Rapid enter/leave mid-transition | Reverses from the current value, never restarts from 0 |
| Press and release | Press channel independent of hover |
| Press then drag the cursor away | Visual releases and the click cancels, matching the existing rule at `SMixtormatIconButton.cpp:91` |
| Selected while hovered | Both channels blend; selection settles correctly |
| Selected to unselected | Returns to the hovered value, not the rest value |
| Disabled | Fixed state; no channel animates |
| Foldout open, close, reverse mid-open | Chevron and tint ramp; body visibility stays immediate |
| Many layer rows present | Only mid-transition rows hold a timer; idle rows cost nothing |
| Repeated mouse movement across the stack | No cumulative timer growth; frame cost stable |
| Popup / help opening and closing | Entrance and exit, if built |
| Live-theme change mid-animation | Workspace rebuilds; new widgets start at target; no stale state, no dangling timer |
| Workspace rebuild while animating | No dangling timer, no stale brush |
| Animations disabled | Every family snaps and behaves exactly as it does today |

Across all of them, watch specifically for: visual jumps at the reversal point; perpetual
Slate invalidation after everything settles; layout jitter from an animated property
leaking into `ComputeDesiredSize`; per-frame allocations; and added input latency.