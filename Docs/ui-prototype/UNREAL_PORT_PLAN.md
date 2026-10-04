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
| Geometry tokens | `Private/Style/MixtormatDesignTokens.h` | `inline float` globals, mutable at runtime; `RecomputeDerived()` exists |
| Color roles | `Private/Style/MixtormatPalette.h` | semantic role functions, `ResolveColor` for live overrides |
| Live theme registry | `Private/Style/MixtormatLiveTheme.h/.cpp` | number/color entries with min, max, default; serialize/deserialize; save/load |
| Slate style set | `Private/Style/MixtormatStyle.cpp`, `MixtormatMutableStyleSet.h` | `Refresh()` rebuilds brushes and text styles in place |
| UI Style popup | `Private/Widgets/SMixtormatLiveThemePanel.cpp`, `SMixtormat_Theme.cpp` | already wired to the top bar's `UI STYLE` button |
| Linear gradients | `Private/UI/Primitives/MixtormatGradientPainter.h/.cpp` | `MixtormatGradient::Paint` with CSS orientation and sRGB interpolation |
| Gradient container | `Private/UI/Primitives/SMixtormatGradientBox.h/.cpp` | wraps content in a painted gradient |
| Icon brushes | `Private/Style/MixtormatStyle.cpp`, `Private/UI/Atoms/MixtormatIcons.cpp` | 57 PNGs registered by role name |
| Preview overlays | `Private/Widgets/SMixtormat_Preview.cpp` `BuildPreviewPanel()` | edge clusters already match the prototype layout |
| Layer stack | `Private/UI/Layers` | rows, group rows, children, badges, chevrons |
| Controls | `Private/UI/Controls`, `Private/UI/Rows` | slider, toggle, segmented, dropdown row/chip |

Gaps confirmed during the audit:

- `MixtormatGradient::Paint` supports linear orientations only. The vignette, the
  radial falloff and any conic effect have no Slate equivalent yet.
- Saturation is not modelled. Every `*-saturation` token in `tokens.css` needs an
  explicit RGB transform in the palette or painter.
- Soft-light and plus-lighter blend modes do not exist in Slate. They are already
  resolved by adding source colour at alpha in `MixtormatPalette`; any new additive or
  soft-light layer must follow that pattern rather than a blend-mode enum.
- No token exists for the shared button style, the collapse-icon states, the layer
  active hairline/glow, the vignettes, or the card gradient reach.
- The UI Style panel has no tabs and no per-section grouping; it is a filtered list.

## Complete token coverage

Every authored token in `tokens.css` must land somewhere in Unreal. Nothing is
carried over as "close enough". This is the checklist.

### Geometry: sizes, gaps, spacing, insets, thickness

| Prototype token group | Unreal destination |
| --- | --- |
| `row-height`, `row-gap`, `paired-gap`, `dropdown-label-gap`, `dragger-text-inset`, `dropdown-label-ratio` | existing `RowHeight`, `RowGap`, `RowLabelGap`, `DraggerTextInset`, `DropdownLabelRatio` |
| `toggle-size`, `toggle-fill-inset`, `icon-size`, `icon-hit-padding`, `button-height` | existing `ToggleSize`, `ToggleFillInset`, `IconButtonSize`, `IconButtonHitSlop`, `ButtonHeight` |
| `foldout-height`, `-gutter`, `-body-top/-bottom`, `-outer-top/-bottom`, `-header-padding-*` | **new** `Foldout*` tokens; `GroupHeaderHeight`/`PanelGutter` partially cover them today |
| `card-*` geometry (radius, header height, four header pads, four outer margins, body pads, header margins, leading gap) | existing `GroupCard*` tokens, renamed to match 1:1 rather than adding a parallel set |
| `group-button-*` (height, three hairline/separator sizes, three opacities) | **new** `GroupButton*` |
| `layer-height`, `child-height`, `layer-group-height`, `layer-gap`, `layer-indent`, `layer-badge-width` | existing `LayerRowHeight`, `LayerChildRowHeight`, `LayerGroupRowHeight`, `LayerRowGap`, `LayerChildIndent`, `BadgeWidth` |
| `layer-hierarchy-line-width/-opacity`, `layer-visibility-size/-radius`, `layer-active-*` | **new** |
| `overlay-control-width` | **new**; overlay width must stop deriving from `InspectorWidth` |
| shell: `left-width`, `inspector-width`, `topbar-height`, `status-height`, `gallery-height`, `panel-padding`, `splitter-size/-hit-size` | existing `LayerStackWidth`, `InspectorWidth`, `TopBarHeight`, `StatusBarHeight`, `PanelPadding`, `SplitterHandleSize/HitSize` |
| popovers: `menu-width`, `menu-row-height`, `menu-padding`, `popup-lip-height`, `help-*` | existing `MenuWidth`, `MenuItemHeight`, `MenuPanelPadding`, `MenuLipHeight` |

### Colour and opacity

| Prototype token group | Unreal destination |
| --- | --- |
| base roles (`ground`, `text`, `accent`, `modified`, `warning`, `shade`, panel/header/hairline/layer/child channels) | existing `MixtormatPalette` roles; RGB-channel triples collapse into single `FLinearColor` roles, so `*-rgb` becomes one role and its separate `-opacity` becomes that role's `A` |
| `*-saturation` (20 tokens: foldout, hairline, card, layer, child, fill, well) | **new**, plus a saturation helper |
| blend-mode names (`plus-lighter`, `multiply`, `soft-light`, `normal`) | **not tokens**. Resolved to explicit colour math at authoring time |
| well border opacities incl. per-endpoint top/bottom and hover variants | **new**; `WellOutline` currently multiplies a flat `0.4f` |
| slider fill ramps (`fill-body-*`, `fill-shade-*`, `fill-disabled-opacity`) | existing `FillTop/Bottom`, `MultiplyStart/Mid/End`; the mid-position and hover/active ramps are **new** |
| layer/child horizontal opacities (`child-*-opacity`, `-left/-right`) | existing `LayerChild*` colors carry baked alpha; split into role + opacity so both are tunable |
| `*-vignette-*`, `foldout-title-opacity`, `card-title-opacity`, `control-label/value-opacity`, `text-muted/disabled-opacity` | **new** where absent |

### Typography

| Prototype token | Unreal destination |
| --- | --- |
| `body-size`, `caption-size`, `dragger-font-size`, `card-title-size`, `foldout-title-size`, `layer-group-title-size`, `group-button-font-size` | existing `FontBody`, `FontCaption`, `FontSliderLabel`, `FontCardTitle`, `FontGroupCardTitle`; foldout and group-button sizes are **new** |
| weights (`value-weight`, `card-title-weight`, `foldout-title-weight`, `group-button-font-weight`, `layer-group-title-weight`) | existing bold sliders (`CardTitleBold`, `GroupCardTitleBold`, `GroupHeaderBold`) are 0..1 switches; the prototype wants real weights, so **new** per-role weight tokens feeding `FSlateFontInfo` |
| tracking (`foldout-title-tracking`, `card-title-tracking`, `group-button-tracking`, `control-label-tracking`) | existing `CaptionLetterSpacing`, `GroupCardTitleLetterSpacing`; the remaining three are **new** |
| `--font-family` (Roboto / Inter) | **not a numeric token**. Slate font faces come from the composite font; Roboto stays default, Inter is an opt-in swap |
| `dragger-label-case` | **prototype-only**. Slate has no per-row transform; either drop it or map to separate style entries |

### What must be built, not just registered

- A saturation transform applied at palette read time.
- Radial/vignette support in `MixtormatGradientPainter` (currently linear only).
- Per-endpoint well border falloff instead of one flat multiplier.
- The sampled power-curve falloff that `falloff.js` implements in six stops.
- A shared `FButtonStyle` family plus a separator child brush.

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
   default, and a category matching the prototype's UI Style section.
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
  label / spin box / reset row, plus one row per palette colour.
- Opened by `UI STYLE` in the top bar (`SMixtormat_Shell.cpp::BuildTopBar`), which
  is only visible when shipped developer sources exist.
- `SMixtormat_Theme.cpp::OpenLiveThemePanel` hosts it in a 620x720 `SWindow`.
- Edits call `OnThemeChanged` -> `RequestThemeRefresh`, so changes repaint live.
  Save/Load go to `Saved/<Product>/LiveTheme.json`; Reset restores authored defaults
  without touching the saved file.

What it does **not** yet have, and what the port still owes it:

| Prototype UI Style | Unreal today | Needed |
| --- | --- | --- |
| Tabs per section | one flat scrollable list, filtered by a search box | tab strip + per-section visibility |
| Two-column layout | one row per token | two-column wrap for numerics |
| Section ordering | registry order in `MixtormatLiveTheme.cpp` | group tokens under the prototype's section names |
| Saturation / falloff / reach tokens | not registered | register with min/max, then wire into paint code |
| Hover help with defaults | `ToolTipText(FromName(Name))` only | show default and range |

Panel work is therefore step 8 below, and it should follow the token registrations:
a control for a token that does not exist yet would be dead UI.

## Work order

Each step is independently reviewable and stops at a screenshot comparison.

1. **Collapse icons.** `FoldoutIconSize`, `FoldoutIconPadding`,
   `FoldoutIconOpacity`, hover/pressed opacity and shade, plus the
   `Mixtormat.Icon.ChevronRight`/`ChevronDown` swaps for both inspector foldouts and
   layer rows. Small, isolated, and it validates the shared-token pattern.
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
9. **Saturation pass.** Apply the twenty `*-saturation` tokens at palette read time,
   so every affected paint layer picks them up at once. Do this before the falloff
   work, because both operate on the same sampled stops.
10. **Typography pass.** Real per-role weights and tracking, replacing the 0..1 bold
   switches once no caller still depends on the switch semantics.

Steps 9 and 10 are wide but shallow; doing them late keeps the earlier visual
comparisons attributable to a single change.

## Risk notes

- Every step above touches a style that other widgets read. Land one token group at a
  time and refresh the whole style set before judging.
- Radius went to zero everywhere except cards in the prototype. `CornerRadius` is
  global in the plugin; the port must use per-surface radii and leave the global
  fallback alone until every caller is checked.
- Layer row heights, indents, and connector geometry are shared by drag/drop and
  expansion code. Confirm drop targeting still lines up after the column changes.
- Preview overlays are built once in `BuildPreviewPanel`. Removing their plates and
  titles is a layout change there, not a style change.
- The prototype's variable fonts have not been validated in Slate. Use static weight
  instances for the engine and keep Roboto as the default family.

## Validation

Static review plus in-engine screenshots for each step. No build, test, or shell
command was run to produce this plan.