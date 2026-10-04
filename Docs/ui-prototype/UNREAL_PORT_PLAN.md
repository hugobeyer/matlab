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

Gaps confirmed during the audit:

- `MixtormatGradient::Paint` supports linear orientations only. `MakeGradient` takes an
  `EOrientation` and a corner-radius vector and nothing else
  (`MixtormatGradientPainter.cpp:88-95`); `AxisPoint` picks one axis
  (`:17-20`). The vignette, the radial falloff and any conic effect have no Slate
  equivalent yet.
- Saturation is not modelled. Every `*-saturation` token in `tokens.css` needs an
  explicit RGB transform in the palette or painter.
- Soft-light and plus-lighter blend modes do not exist in Slate. Plus-lighter is
  already resolved by adding source colour at alpha in two places —
  `GroupCardBackground` (`MixtormatPalette.h:47-50`) and the rail-button plate
  (`MixtormatStyle.cpp:311-314`) — so any new additive layer follows that pattern rather
  than a blend-mode enum. Soft-light has **no** such precedent: the only two uses
  (`--foldout-accent-blend-mode`, `--layer-group-blend-mode`) become an explicit
  darkened source, not an add.
- No token exists for the shared button style, the collapse-icon states, the layer
  active hairline/glow, the vignettes, or the card gradient reach.
- The UI Style panel has no tabs and no per-section grouping; it is a filtered list.
- Overlay clusters are plated. `BuildPreviewPanel` wraps each of seven clusters in an
  `SMixtormatGradientBox` painted `OverlayPlateTop`/`OverlayPlateBottom`
  (`Mixtormat_Preview.cpp:1390`, `:1402`, `:1459`, `:1469`, `:1482`, `:1494`, `:1505`) and
  rounds it by the global `CornerRadius`. The prototype's overlays carry no plate, no
  title and no drag (`README.md:84`), and the prototype's radius is zero everywhere but
  cards. Removing a plate is a layout change, not a restyle.
- Overlay cluster width is derived, not authored: `InspectorWidth * 0.5f`
  (`Mixtormat_Preview.cpp:1397`, `:1489`, `:1512`).
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

## Complete token coverage

Every authored token in `tokens.css` must land somewhere in Unreal. Nothing is
carried over as "close enough". This is the checklist.

### Geometry: sizes, gaps, spacing, insets, thickness

| Prototype token group | Unreal destination |
| --- | --- |
| `row-height`, `row-gap`, `paired-gap`, `dropdown-label-gap`, `dragger-text-inset`, `dropdown-label-ratio` | existing `RowHeight`, `RowGap`, `RowLabelGap`, `DraggerTextInset`, `DropdownLabelRatio` |
| `toggle-size`, `toggle-fill-inset`, `icon-size`, `icon-hit-padding`, `button-height` | existing `ToggleSize`, `ToggleFillInset`, `IconButtonSize`, `IconButtonHitSlop`, `ButtonHeight` |
| `foldout-height`, `-gutter`, `-body-top/-bottom`, `-outer-top/-bottom`, `-header-padding-*` | **new** `Foldout*` tokens; `GroupHeaderHeight`/`PanelGutter` partially cover them today |
| `card-*` geometry (radius, header height, four header pads, four outer margins, body pads, header margins, leading gap) | **new**, added alongside `GroupCard*` rather than renaming it — see "Naming collisions" below |
| `group-button-*` geometry (`group-button-height`, `-hairline-width`, `-separator-width`, `-separator-height`) | **new** `GroupButton*`. `ButtonHeight` is the generic button height, not this family |
| `layer-height`, `child-height`, `layer-group-height`, `layer-gap`, `layer-indent`, `layer-badge-width` | existing `LayerRowHeight`, `LayerChildRowHeight`, `LayerGroupRowHeight`, `LayerRowGap`, `LayerChildIndent`, `BadgeWidth`. `BadgeWidth` is `constexpr` (`MixtormatDesignTokens.h:281`), so it is fixed until that changes |
| `layer-hierarchy-line-width`, `layer-visibility-size/-radius`, `layer-active-*` | **new**. `LayerConnectorOpacity` (`MixtormatDesignTokens.h:496`) already carries the connector opacity at 0.45, so only the width is new |
| `well-border-width`, `well-radius` | **new**. `OutlineWidth` is one global stroke weight and `CornerRadius` is a global radius; the prototype's well is a 1px border on a 0px radius, which the globals cannot express together |
| `overlay-control-width` | **new**; overlay width must stop deriving from `InspectorWidth` |
| shell: `left-width`, `inspector-width`, `topbar-height`, `status-height`, `panel-padding`, `splitter-size/-hit-size` | existing `LayerStackWidth`, `InspectorWidth`, `TopBarHeight`, `StatusBarHeight`, `PanelPadding`, `SplitterHandleSize/HitSize` |
| popovers: `menu-width`, `menu-row-height`, `menu-padding`, `popup-lip-height`, `help-*` | `MenuWidth`, `MenuPanelPadding` map directly. `MenuItemHeight` and `MenuLipHeight` exist but are derived (see gaps), so `menu-row-height` and `popup-lip-height` need a `RecomputeDerived` change. `help-max-width`, `help-padding` and `help-delay` have **no** destination: the plugin has no hover-help surface, only editor `SToolTip`s |
| per-role icon sizes (`foldout-icon-size`, `card-icon-size`, `layer-icon-size`, `layer-module-icon-size`, `topbar-icon-size`, `overlay-icon-size`, `menu-icon-size`, `toolbar-icon-size`, `rail-icon-size`, `icon-sheet-preview-size`) | **new**, one token each. `MenuIconSize` and `ToolbarIconSize` exist but describe a different role. The prototype comment at `tokens.css:230-235` is explicit that equal values are not one role |
| `foldout-icon-padding`, `card-eye-size` | **new**. `ChevronSize` (`MixtormatDesignTokens.h:205`) and `LayerEyeSize` (`:493`) already exist and cover the glyph box, not the padding around it |
| `thumbnail-size`, `thumbnail-radius`, `gallery-swatch-radius`, `gallery-tile-size`, `gallery-gap` | **new**. `LayerThumbnailSize` covers `layer-thumbnail` only, not the generic tile |
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
| `body-size`, `caption-size`, `dragger-font-size`, `card-title-size`, `foldout-title-size`, `layer-group-title-size`, `group-button-font-size`, `control-label-size` | existing `FontBody`, `FontSliderLabel`, `FontCardTitle`, `FontGroupCardTitle`. `caption-size` and `dragger-font-size` are **new**: `FontCaption` (8px) and `FontDragGhostLabel` (9px) are different roles from the prototype's 11px caption and 10px dragger. `foldout-title-size` and `group-button-font-size` are **new** |
| `control-label-size` | `FontSliderLabel` is locked to `FontBody - 1` (`MixtormatDesignTokens.h:559`) and the prototype wants both at 10px, so this is a `RecomputeDerived` change, not a token |
| weights (`value-weight`, `card-title-weight`, `foldout-title-weight`, `group-button-font-weight`, `layer-group-title-weight`, `control-label-weight`) | existing bold sliders (`CardTitleBold`, `GroupCardTitleBold`, `GroupHeaderBold`) are 0..1 switches; the prototype wants real weights, so **new** per-role weight tokens feeding `FSlateFontInfo` |
| tracking (`foldout-title-tracking`, `card-title-tracking`, `group-button-tracking`, `control-label-tracking`) | existing `CaptionLetterSpacing` and `GroupCardTitleLetterSpacing` are in 1/1000 em, not pixels, and are baked into specific style entries (`MixtormatStyle.cpp:467`, `:627`, `:640`, `:651`). The prototype authors px, so the mapping is a unit conversion. `card-title-tracking` maps to the wrong entry today — `Mixtormat.CardTitle` reads `CaptionLetterSpacing`, not the group-card one — and the other three have no existing entry at all |
| `--font-family` (Roboto / Inter) | **not a numeric token**. Slate font faces come from the composite font; Roboto stays default, Inter is an opt-in swap |
| `dragger-label-case` | **prototype-only**. Slate has no per-row transform; either drop it or map to separate style entries |

### Naming collisions: add alongside, do not rename

The `card-*` row above cannot be a 1:1 rename of `GroupCard*`. Three reasons, in
order of cost:

1. **The token names are the persisted keys.** `THEME_NUMBER` stringifies its second
   argument as the registry name (`MixtormatLiveTheme.cpp:47-48`) and `Serialize` writes
   `Entry.Name` as the JSON key (`:261`). A rename orphans every saved
   `Saved/Mixtormat/LiveTheme.json`, and `Deserialize` rejects the whole document on one
   unknown numeric key (`:302-307`) — a user's theme stops loading, it does not half-load.
2. **A 1:1 rename to `Card*` collides with an existing `Card*` family.**
   `CardPadding`, `CardGap` and `CardTitleGap` (`MixtormatDesignTokens.h:331-334`) belong
   to the compact popover card layout, deliberately kept separate from the inspector card
   (`:336-337`). `GroupCard*` names carry that distinction; `Card*` would erase it.
3. **The readers are scattered.** `SMixtormatInspectorCard.cpp` reads seventeen distinct
   `GroupCard*` tokens across `Construct` and `OnPaint` (`:37`, `:40`, `:56`, `:60-84`,
   `:90`, `:94`, `:111`, `:114`, `:185-192`, `:198-204`); `GroupCardLeadingIconSize`
   additionally reaches four inspector builders (`MixtormatInspectorHeight.cpp:65`, `:72`,
   `MixtormatInspectorIds.cpp:443`, `MixtormatInspectorLayer.cpp:180`); and
   `MixtormatStyle.cpp:645-654` builds the `Mixtormat.GroupCardTitle` text style from
   `FontGroupCardTitle`, `GroupCardTitleBold` and `GroupCardTitleLetterSpacing`.
   `MixtormatLiveTheme.cpp:82-102` registers twenty `GroupCard*` entries and
   `MixtormatLiveTheme.cpp:192`, `:194` register two `GroupCard*` colours.

Recommendation: add the prototype-named set alongside `GroupCard*` under a distinct
prefix — `CardHeader*` / `CardBody*` / `CardOuter*` are free, and reusing the bare
`Card*` prefix would collide with reason 2 — then move callers one file at a time and
delete the old set in a later pass. If the keys must change in the same pass, teach
`Deserialize` to accept the old key and emit the new one: it is already the only place
that validates, and it already runs `Reset()` before applying (`:334-341`).

One caller is already wrong in the direction the port is heading.
`MakeFeaturePreviewButton` defaults its icon to `LayerEyeSize` (`Mixtormat.h:213`), a
layer-stack token, and its four card callers all override it with
`GroupCardLeadingIconSize` anyway. The add-alongside pass is the moment to give it a
card default.

### What must be built, not just registered

- A saturation transform applied at palette read time.
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
  brushes. The saturation pass therefore has to work at palette read time; a
  `Refresh()`-only change will reach the layer states and nothing else.
- **Drag/drop targeting is fraction-normalised, not pixel-sized.** `LocalFraction` divides
  by the row's own height and is compared against `0.5` and
  `GroupRowIntoZoneFraction` (`SMixtormatDropTargets.h:92-132`), so changing
  `LayerRowHeight`, `LayerChildRowHeight` or `LayerRowGap` does not break it. The real
  exposure is that the "into this group" band is `GroupRowIntoZoneFraction *
  LayerGroupRowHeight` in absolute pixels, and the prototype's 18px group row makes that
  band 4.5px rather than 5.5px.
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
  one rounds by the global `CornerRadius`. The two clusters whose width is
  `InspectorWidth * 0.5f` (`:1397`, `:1489`, `:1512`) are the ones `overlay-control-width`
  replaces.
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
(`group-button-*`). In Slate this is one `FButtonStyle` family:

- `Mixtormat.GroupButton`, `.GroupButtonHover`, `.GroupButtonSelected`,
  `.GroupButtonPressed`, `.GroupButtonDisabled`
- Text style `Mixtormat.GroupButtonText` from `group-button-font-size`, `-weight`,
  `-tracking`, `-text-opacity`.
- Top hairline drawn in the normal brush; separators drawn as a 1px child border
  between cells, matching `--group-button-separator-*`.

Existing `Mixtormat.TopButton` and the tab styles keep their current names until the
new family is verified; the old entries stay registered so nothing regresses.

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

Panel work is therefore step 8 below, and it should follow the token registrations:
a control for a token that does not exist yet would be dead UI.

## Work order

Each step is independently reviewable and stops at a screenshot comparison.

1. **Collapse icons.** `FoldoutIconSize`, `FoldoutIconPadding`,
   `FoldoutIconOpacity`, hover/pressed opacity and shade, plus the
   `Mixtormat.Icon.ChevronRight`/`ChevronDown` swaps for both inspector foldouts and
   layer rows. The two icon brushes are already registered; this step is the state
   styling and the swap. Small, isolated, and it validates the shared-token pattern.
   Note that `ChevronSize` is shared with the chip, the menu item, the layer group row
   and the child-output preview (`SMixtormatLayerGroupRow.cpp:166`,
   `SMixtormatLayerRow.cpp:216`, `SMixtormatChip.cpp:54`, `SMixtormatMenuItem.cpp:81`),
   and the preview uses `ChevronSize * 0.6f` (`SMixtormat_Preview.cpp:759-761`), so
   retuning it for foldouts moves five callers. `FoldoutIconSize` is a separate new
   token for exactly this reason.
2. **Layer visibility squircles.** Replace the layer eye brush with a rounded square
   at `LayerVisibilitySize` and `LayerVisibilityRadius`. Keeps `data-action=eye`
   behaviour and tooltip text unchanged.
3. **Shared button family.** Add the tokens and the five `FButtonStyle`s, then move
   tabs, segmented controls, and the top bar onto them one caller at a time.
4. **Layer active states.** Accent hairline plus downward-fading glow
   (`LayerActiveHairline*`, `LayerActiveGlow*`), including the group-selected case.
5. **Card gradient reach.** Port the reach and mirrored tail into
   `SMixtormatGradientBox`/the card painter. Requires the sampled power curve, which
   `falloff.js` reproduces with six stops; port the sampling, not the JavaScript.
6. **Vignettes.** New radial support in `MixtormatGradientPainter` plus
   `PanelVignette*`, `CardVignette*`, `FoldoutVignette*`. Explicitly multiply-only.
7. **Dropdown presentation.** The prototype's menu-styled dropdown maps to the
   existing `SMixtormatChip`/`MixtormatRow::MakeDropdown` popup path, restyled with
   the menu surface. Binding and value handling are untouched.
8. **UI Style panel parity.** Tabs per section, matching category names, two-column
   numeric layout, and hover help carrying each token's default and range.
9. **Saturation pass.** Apply the twenty-three `*-saturation` tokens at palette read
   time, so every affected paint layer picks them up at once. Decide the two
   backdrop-filter cases separately. Do this before the falloff work, because both
   operate on the same sampled stops.
10. **Typography pass.** Real per-role weights and tracking, replacing the 0..1 bold
   switches once no caller still depends on the switch semantics. Three bold switches
   exist (`CardTitleBold`, `GroupCardTitleBold`, `GroupHeaderBold`) and three read sites
   (`MixtormatStyle.cpp:179`, `:633`, `:645`) via `Weight()`, which is a hard
   `>= 0.5 ? Bold : Regular` (`MixtormatStyle.cpp:50-54`). Reaching a 400/600 pair needs
   `FSlateFontInfo` typefaces that the default font family may not carry.
11. **`RecomputeDerived()` unlock.** `MenuItemHeight`, `MenuLipHeight`, `TabHeight`,
   `SegmentHeight` and `FontSliderLabel` are all derived
   (`MixtormatDesignTokens.h:549-560`). Nothing above reaches its authored value until
   this lands, and step 10 depends on it. Keep it separate from step 10 so a weight
   regression is not confused with a layout one.

Steps 9, 10 and 11 are wide but shallow; doing them late keeps the earlier visual
comparisons attributable to a single change. Step 11 gates several of the tokens the
earlier steps add, so it cannot move past them.

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
  are fraction-normalised (`SMixtormatDropTargets.h:95-132`), so height and gap changes
  are safe. The narrow band is the group "into" zone: it shrinks in absolute pixels as
  `LayerGroupRowHeight` goes from 22 to the prototype's 18.
- Preview overlays are built once in `BuildPreviewPanel`. Removing their plates and
  titles is a layout change there, not a style change, and it takes the cluster inset
  with it.
- The prototype's variable fonts have not been validated in Slate. Use static weight
  instances for the engine and keep Roboto as the default family.

## Validation

Static review plus in-engine screenshots for each step. No build, test, or shell
command was run to produce this plan.