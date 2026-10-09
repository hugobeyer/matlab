# UI style token audit

Status: audit, 2026-10-07; partially superseded by the current UI STYLE implementation.
Source wins; verify line numbers before acting. The tables below are not implementation tasks.
Scope: `Source/MixtormatEditor/Private` — spacing, padding, sizing, opacity, colour,
typography/boldness, icons, gradients, borders and corner radius.

## The rule

Widgets must not introduce local styling values. Style metrics and authored colours
come from one of two systems (data ranges, state geometry and identity multipliers
are not authored styling):

| System | Lives in | Use for |
|---|---|---|
| `MixtormatTokens` | `Style/MixtormatDesignTokens.h` | Structural tokens: row heights, insets, ramp sizes, brand proportions. `constexpr` constants or mutable `inline float` values; the latter are not compile-time constants |
| `FMixtormatTheme` + schema | `Style/MixtormatTheme.h`, `MixtormatThemeSchema.cpp`, `Config/UIStyleTheme.json` | Anything the user should retune live in the UI STYLE panel. Tabs: Global, Controls, Foldouts, Cards, Layers, Buttons, Menus, Preview, … |

Read them through `FMixtormatThemeStore::GetResolved()` (or `GetTheme()`), never by
copying a value into a widget. A third copy of a number is the bug this audit exists
to prevent.

The table below lists existing violations, not permission for new local styling.
Tokenize affected controls when extracting/reusing them, without silently changing
appearance or behaviour. Suggested token homes are proposals: preserve existing
values unless a design change is separately approved.

## Coverage — what is already tokenized

| Category | Token source |
|---|---|
| Spacing / padding | `ControlLayout.RowGap`, `CardLayout.Gap`, `GalleryLayout.TileGap`, `PreviewLayout.OverlayButtonGap` / `ToolbarGap` / `TogglePadding`, `ControlLayout.InspectorFeatureButtonGap`, `MenuLayout.*`, `Well.*`, `MixtormatTokens::InspectorTopMargin` |
| Sizing | `Icons.Roles[].GlyphSize` / `ButtonSize`, `LayerLayout.RowHeight` / `ThumbnailSize`, `PreviewLayout.ResolutionControlWidth`, `MenuLayout.Width` / `LipHeight` / `RowHeight`, `SegmentHeight`, `ScalarRampHeight`, `ColorRampHeight`, `MixtormatTokens::InspectorWidth` |
| Opacity | `ControlLabelOpacity`, `ControlValueOpacity`, `TextDisabledOpacity`, `ZeroTickOpacity`, `SegmentShadeAlpha`, `FillDisabledSaturation`, `ToggleDisabledShadeTop/Bottom`, `FoldoutTitleDisabledOpacity`, `ScrollbarThumbOpacity` / `HoverOpacity`, `Well.BorderOpacity`, `Preview.PlateOpacity`, `MixtormatTokens::InspectorOverlayBackgroundOpacity` |
| Colour | `EMixtormatColorRole` (14 roles) via `Palette.Get(...)`; `FMixtormatColorRef` adds opacity, saturation and a per-channel multiplier without inventing a palette entry |
| Typography / boldness | `EMixtormatTextRole` (15 roles) + `FMixtormatTextSpec` through `FMixtormatTypography`. Weight lives in the spec, never per widget |
| Icons | `EMixtormatIconRole` for size, `MixtormatIcons::*` for the brush. `LayerVisToggle` additionally owns state blend controls; removed roles are not candidates for reuse. |
| Ramp canvases | Resolved `ControlLayout.ScalarRamp*` metrics for geometry, grid/background/shade/border paint; `ScalarRampButton.*` styles shared ramp-toolbar plates. |
| Gradients | `MixtormatTokens` gradient block (`GradientSamples`, `MultiplyMidPosition`, …) consumed by `MixtormatGradientPainter`, `MixtormatSurfacePainter`, `MixtormatWell` |
| Borders / radius | `ControlLayout.CornerRadius`, `MixtormatTokens::CornerRadius` / inner-corner value, `Well.BorderWidth` / `BorderOpacity`, `Gallery.BorderWidth`, `MixtormatTokens::InspectorHairlineThickness` |

## Approved overlay-workspace additions — UI STYLE token plan

D30–D32 add overlay navigation and a bottom gallery drawer. When implementing,
put user-retunable appearance/layout defaults in `FMixtormatTheme` plus
`MixtormatThemeSchema.cpp` and `Config/UIStyleTheme.json`, grouped under the existing
**Preview → Layout** and **Gallery → Gallery Layout** UI STYLE sections. Preserve the
existing defaults as the starting values. Do not expose live panel geometry that the
user has already dragged; geometry stays session state.

| UI STYLE control to add | Theme metric / section | Purpose |
|---|---|---|
| Rail edge inset | `FMixtormatPreviewMetrics`, Preview → Layout | Gap from viewport edge to the pinned rail |
| Rail-to-panel gap | `FMixtormatPreviewMetrics`, Preview → Layout | Separation between rail and shared left overlay |
| Left overlay initial width | `FMixtormatPreviewMetrics`, Preview → Layout | First-placement width only; subsequent user resize is retained runtime state |
| Left overlay surface opacity | Existing overlay theme or a dedicated Preview metric | Tune the translucent shared Layers/Library/Global surface without local alpha literals |
| Marking-menu centre gap and row gap | `FMixtormatPreviewMetrics`, Preview → Layout | Retune the expanded four-card Q-menu arrangement |
| Marking-menu guide length/thickness/opacity and bloom size/opacity | `FMixtormatPreviewMetrics`, Preview → Layout | Retune the faded hairline cross and soft center bloom; reuse the palette TextMuted role |
| Gallery drawer edge inset | `FMixtormatGalleryMetrics`, Gallery → Gallery Layout | Gap from viewport bottom/edges |
| Gallery drawer initial height | `FMixtormatGalleryMetrics`, Gallery → Gallery Layout | First-placement height only; user resize remains runtime state |
| Gallery drawer header/mode-switch gap | `FMixtormatGalleryMetrics`, Gallery → Gallery Layout | Space around MATERIALS/MASKS mode selector |
| Gallery drawer surface opacity | Existing gallery surface theme or a dedicated Gallery metric | Tune drawer surface treatment without local alpha literals |

The current marking-menu guide/spacing values are structural `MixtormatTokens`
for the recent prototype. If promoted to UI STYLE, move them into theme metrics rather
than keeping parallel token copies. Add only metrics actually consumed by the UI.
Use `EMixtormatThemeRefreshMode::Reconstruct` for dimensions that require rebuilding;
use live refresh only for values the painter can safely re-read. Keep structural
constraints (min resize sizes, grip target sizes, snap threshold, layout bounds) in
`MixtormatTokens`, not UI STYLE. Keep rail glyph/button size sourced from the existing
`NavigationRail` icon role; `PanelToolbar` was removed.
The existing shell splitter style remains for the docked Inspector and must not be
removed merely because the left/gallery splitters disappear.

## Gaps — local literals that must be tokenized

| File | Line | Literal | What it is | Suggested home |
|---|---|---|---|---|
| `Widgets/SMixtormat_Inspector.cpp` | 459 | `2.0f`, `3.0f` | Selection header slot padding — only the top edge is `InspectorTopMargin` | `InspectorHeaderInsetX` / `InspectorHeaderInsetBottom` tokens |
| `Widgets/SMixtormat_Inspector.cpp` | 656 | `ContentPadding(2.0f)` | Base Color swatch button content padding | Swatch-specific padding retaining 2; `ControlLayout.ButtonPaddingCompact` defaults to 7 (`MixtormatTheme.h` L593), so substituting it is not neutral tokenization |
| `Widgets/SMixtormat_Shell.cpp` | 587 | `CopyWithNewOpacity(0.5f)` | GLOBAL empty-state text opacity | An empty-state opacity token |
| `Widgets/SMixtormat_Preview.cpp` | 907 | `Padding(1.0f, 0, 0, 0)` | Child-output pill: gap before the eye | One pill token set |
| `Widgets/SMixtormat_Preview.cpp` | 947 | `Padding(FMargin(2.0f, 0.0f))` | Child-output pill outer padding | Same |
| `Widgets/SMixtormat_Preview.cpp` | 954 | `Padding(1.0f, 3.0f)` | Hairline separator inset inside the pill | Same |
| `Widgets/SMixtormat_Preview.cpp` | 965 | `Padding(2.0f, 0, 2.0f, 0)` | Chevron inset inside the pill | Same |
| `Widgets/Inspector/MixtormatInspectorGenerators.cpp` | 1134 | `Padding(4.0f, 0, 0, 0)` | “Presets” label inset in the ramp-preset combo | `MenuLayout` / `ControlLayout` token |
| `Widgets/Inspector/MixtormatInspectorGenerators.cpp` | 1138 | `Padding(2.0f, 0, 2.0f, 0)` | Chevron inset in the same combo | Same |
| `UI/Controls/SMixtormatColorRamp.cpp` | 406 | `FLinearColor(0.7f, 0.7f, 0.7f)` | Hardcoded grey | A palette role |
| `UI/Controls/SMixtormatTabStrip.cpp` | 127 | `FLinearColor(1, 1, 1, 0.55f)` | Hardcoded inactive-tab foreground | Theme colour/opacity retaining existing white and 0.55; replacing it with a nonwhite palette role is a separate appearance change |

The child-output pill is the worst offender: four literals describing one control, so
its spacing cannot be retuned as a unit.

## Intentional non-tokens

Do not treat these as local widget-styling gaps:

- `Preview/SMixtormatLightGizmo.cpp` L98 `FLinearColor(0, 0, 0, 1)` — render-target clear colour.
- `Widgets/Layers/MixtormatLayerActions.cpp` L32 `FLinearColor(0.2f, 0.2f, 0.2f, 1.0f)` — a new layer's default base colour (data default).
- `Widgets/Layers/MixtormatLayerHierarchy.cpp` L925 and `MixtormatLayerMenus.cpp` L539 — transparent-black sentinels.
- `Style/MixtormatTheme.cpp` — the default theme itself; it is where palette values are authored.
- `Style/MixtormatStyle.cpp` — builds brushes from tokens; `CornerRadius` hits there are the style set, not widget literals.
- `UI/Containers/SMixtormatInspectorCard.cpp` L55–57 — white RGB is an identity
  multiplier; alpha already reads title-opacity tokens. The actual text colour is
  palette Text at L36/L42. Replacing the multiplier with Text would double-tint it.
- Split fractions and user-dragged panel geometry are runtime state, not theme
  values; see `FMixtormatShellMetrics`. `InspectorWidth` is a structural token in
  `MixtormatDesignTokens.h` L626, not a user-dragged geometry field.

## Checklist for new UI

1. Styling padding and size defaults read tokens/theme values; user-dragged geometry
   reads retained layout state. Identity values (zero padding, unit multipliers) and
   data ranges are not invented style constants.
2. Authored colours/opacity use palette/theme/tokens. Preserve identity multipliers
   and data colours; do not replace every white literal with a palette tint.
3. Text uses a `EMixtormatTextRole` spec — never a per-widget font size or weight.
4. Icons go through `MixtormatIcons::*` and an icon role for size.
5. Corner radius and border width come from `ControlLayout` / `Well` / the token block.
6. If a value should be retunable live, add it to `FMixtormatTheme` **and** the schema; if it is structural, add a token. Never inline it.
7. A new token needs a comment saying what it is for, in the style of the existing blocks.

## Related

- `AgentDocs/UI.md` — theme/style/tokens section and the widget map.
- `Style/MixtormatDesignTokens.h` — the token blocks, each with its rationale.
- `AgentDocs/old_docs/viewport-quick-controls-plan.md` — archived quick-controls delivery history.
