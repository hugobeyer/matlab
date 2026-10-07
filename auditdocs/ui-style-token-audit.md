# UI style token audit

Status: audit, 2026-10-07. Source wins; verify line numbers before acting.
Scope: `Source/MixtormatEditor/Private` — spacing, padding, sizing, opacity, colour,
typography/boldness, icons, gradients, borders and corner radius.

## The rule

No widget spells a style value. Every number and colour comes from one of two systems:

| System | Lives in | Use for |
|---|---|---|
| `MixtormatTokens` | `Style/MixtormatDesignTokens.h` | Structural constants: row heights, insets, ramp sizes, brand proportions. Compile-time (`inline float` / `constexpr`) |
| `FMixtormatTheme` + schema | `Style/MixtormatTheme.h`, `MixtormatThemeSchema.cpp`, `Config/UIStyleTheme.json` | Anything the user should retune live in the UI STYLE panel. Tabs: Global, Controls, Foldouts, Cards, Layers, Buttons, Menus, Preview, … |

Read them through `FMixtormatThemeStore::GetResolved()` (or `GetTheme()`), never by
copying a value into a widget. A third copy of a number is the bug this audit exists
to prevent.

## Coverage — what is already tokenized

| Category | Token source |
|---|---|
| Spacing / padding | `ControlLayout.RowGap`, `CardLayout.Gap`, `GalleryLayout.TileGap`, `PreviewLayout.OverlayButtonGap` / `ToolbarGap` / `TogglePadding`, `ControlLayout.InspectorFeatureButtonGap`, `MenuLayout.*`, `Well.*`, `MixtormatTokens::InspectorTopMargin` |
| Sizing | `Icons.Roles[].GlyphSize` / `ButtonSize`, `LayerLayout.RowHeight` / `ThumbnailSize`, `PreviewLayout.ResolutionControlWidth`, `MenuLayout.Width` / `LipHeight` / `RowHeight`, `SegmentHeight`, `ScalarRampHeight`, `ColorRampHeight`, `MixtormatTokens::InspectorWidth` |
| Opacity | `ControlLabelOpacity`, `ControlValueOpacity`, `TextDisabledOpacity`, `ZeroTickOpacity`, `SegmentShadeAlpha`, `FillDisabledSaturation`, `ToggleDisabledShadeTop/Bottom`, `FoldoutTitleDisabledOpacity`, `ScrollbarThumbOpacity` / `HoverOpacity`, `Well.BorderOpacity`, `Preview.PlateOpacity`, `MixtormatTokens::InspectorOverlayBackgroundOpacity` |
| Colour | `EMixtormatColorRole` (14 roles) via `Palette.Get(...)`; `FMixtormatColorRef` adds opacity, saturation and a per-channel multiplier without inventing a palette entry |
| Typography / boldness | `EMixtormatTextRole` (15 roles) + `FMixtormatTextSpec` through `FMixtormatTypography`. Weight lives in the spec, never per widget |
| Icons | `EMixtormatIconRole` for size, `MixtormatIcons::*` for the brush. A widget never spells a style key |
| Gradients | `MixtormatTokens` gradient block (`GradientSamples`, `MultiplyMidPosition`, …) consumed by `MixtormatGradientPainter`, `MixtormatSurfacePainter`, `MixtormatWell` |
| Borders / radius | `ControlLayout.CornerRadius`, `MixtormatTokens::CornerRadius` / inner-corner value, `Well.BorderWidth` / `BorderOpacity`, `Gallery.BorderWidth`, `MixtormatTokens::InspectorHairlineThickness` |

## Gaps — local literals that must be tokenized

| File | Line | Literal | What it is | Suggested home |
|---|---|---|---|---|
| `Widgets/SMixtormat_Inspector.cpp` | 459 | `2.0f`, `3.0f` | Selection header slot padding — only the top edge is `InspectorTopMargin` | `InspectorHeaderInsetX` / `InspectorHeaderInsetBottom` tokens |
| `Widgets/SMixtormat_Inspector.cpp` | 656 | `ContentPadding(2.0f)` | Base Color swatch button content padding | `ControlLayout.ButtonPaddingCompact` (already exists) |
| `Widgets/SMixtormat_Shell.cpp` | 587 | `CopyWithNewOpacity(0.5f)` | GLOBAL empty-state text opacity | An empty-state opacity token |
| `Widgets/SMixtormat_Preview.cpp` | 907 | `Padding(1.0f, 0, 0, 0)` | Child-output pill: gap before the eye | One pill token set |
| `Widgets/SMixtormat_Preview.cpp` | 947 | `Padding(FMargin(2.0f, 0.0f))` | Child-output pill outer padding | Same |
| `Widgets/SMixtormat_Preview.cpp` | 954 | `Padding(1.0f, 3.0f)` | Hairline separator inset inside the pill | Same |
| `Widgets/SMixtormat_Preview.cpp` | 965 | `Padding(2.0f, 0, 2.0f, 0)` | Chevron inset inside the pill | Same |
| `Widgets/Inspector/MixtormatInspectorGenerators.cpp` | 1134 | `Padding(4.0f, 0, 0, 0)` | “Presets” label inset in the ramp-preset combo | `MenuLayout` / `ControlLayout` token |
| `Widgets/Inspector/MixtormatInspectorGenerators.cpp` | 1138 | `Padding(2.0f, 0, 2.0f, 0)` | Chevron inset in the same combo | Same |
| `UI/Controls/SMixtormatColorRamp.cpp` | 406 | `FLinearColor(0.7f, 0.7f, 0.7f)` | Hardcoded grey | A palette role |
| `UI/Controls/SMixtormatTabStrip.cpp` | 127 | `FLinearColor(1, 1, 1, 0.55f)` | Hardcoded white and alpha | Palette role + opacity token |
| `UI/Containers/SMixtormatInspectorCard.cpp` | 55 | `FLinearColor(1, 1, 1, …)` | Card title tint literal | Palette role |

The child-output pill is the worst offender: four literals describing one control, so
its spacing cannot be retuned as a unit.

## Intentional non-tokens

Do not “fix” these — they are not styling:

- `Preview/SMixtormatLightGizmo.cpp` L98 `FLinearColor(0, 0, 0, 1)` — render-target clear colour.
- `Widgets/Layers/MixtormatLayerActions.cpp` L32 `FLinearColor(0.2f, 0.2f, 0.2f, 1.0f)` — a new layer's default base colour (data default).
- `Widgets/Layers/MixtormatLayerHierarchy.cpp` L925 and `MixtormatLayerMenus.cpp` L539 — transparent-black sentinels.
- `Style/MixtormatTheme.cpp` — the default theme itself; it is where palette values are authored.
- `Style/MixtormatStyle.cpp` — builds brushes from tokens; `CornerRadius` hits there are the style set, not widget literals.
- Panel widths and split ratios (`InspectorWidth`, `ShellLeftFraction`, …) — deliberately runtime state, not theme values; see the `FMixtormatShellMetrics` comment.

## Checklist for new UI

1. Every `Padding`, `FMargin`, `WidthOverride`, `HeightOverride` reads a token or a resolved theme value.
2. Every colour is a palette role; opacity comes from a token, not a literal alpha.
3. Text uses a `EMixtormatTextRole` spec — never a per-widget font size or weight.
4. Icons go through `MixtormatIcons::*` and an icon role for size.
5. Corner radius and border width come from `ControlLayout` / `Well` / the token block.
6. If a value should be retunable live, add it to `FMixtormatTheme` **and** the schema; if it is structural, add a token. Never inline it.
7. A new token needs a comment saying what it is for, in the style of the existing blocks.

## Related

- `AgentDocs/UI.md` — theme/style/tokens section and the widget map.
- `Style/MixtormatDesignTokens.h` — the token blocks, each with its rationale.
- `auditdocs/workspace-layout/viewport-quick-controls-plan.md` — the pending viewport work must follow this rule.
