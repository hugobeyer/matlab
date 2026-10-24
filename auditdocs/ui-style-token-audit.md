# UI style token audit

Status: audit, 2026-10-07; partially superseded by the current UI STYLE implementation.
Source wins; verify line numbers before acting. The tables below are not implementation tasks.
Scope: `Source/MixtormatEditor/Private` — spacing, padding, sizing, opacity, colour,
typography/boldness, icons, gradients, borders and corner radius.

## Current release priority — hover, popovers and dialogs (2026-10-24)

Targeted static check, not visual validation. Fix styling consistency before optional
file splits. Moss and GPU-pass internals are outside this work.

| Area | Confirmed source evidence | Next action |
|---|---|---|
| Hover help — patched 2026-10-24 | Status dots, both tab helpers, gallery drawer header and layer/group Noise-mask rows now use `SMixtormatHelp::MakeStyledToolTip`. | Owner verifies hover appearance and input behavior; this does not certify every tooltip in the plugin. |
| Ramp stop row — fixed | `SMixtormatColorRamp.cpp` uses resolved `ControlLabel` typography; `ColorRampStopLabelGap`, `ColorRampStopSwatchWidth/Height` and `ColorRampChromeGap` retain 6px, 22×16 and 2px. The 8px gap reuses `ScalarRampToolbarGroupGap`. | New metrics are in theme/schema/JSON; generic swatches are currently 32×12 and `RowLabelGap` is 4, so substituting those would change appearance. Stop dragging, endpoint locks, picker callbacks and the already-styled hover remain unchanged. |
| Tab styling — fixed | `MakeTab` reads `ControlLayout.TabInactiveOpacity` (0.55) from the resolved theme. | Retains neutral white without adding a palette tint; active foreground and both existing tooltip adapters are unchanged. |
| Bake settings dialog — fixed | `SMixtormatBakeSettingsDialog.cpp` uses resolved `ControlLabel`, `Caption` and `Body` roles with Text/TextMuted/Error palette colors, not local font weights or inherited/subdued foreground. | Summary leading retains 1.35 via `BakeDialogSummaryLineHeight` in theme/schema/JSON. Shared scrollbar styling and wrapping cover long output names. Native picker/menu internals still need owner checks. |
| Bake result dialog — patched in this follow-up | `SMixtormatBakeResultDialog.cpp` now uses the existing `Mixtormat.Window`, `Mixtormat.ScrollBar` and `Mixtormat.TopButton` styles, resolved scrollbar thickness, Body message and ControlLabel action typography with palette Text. | Previously used `ToolPanel.GroupBorder` and default Slate text/scrollbar/buttons. Modal ownership, message wrapping, window dimensions, footer layout and all five action callbacks remain unchanged; owner visual checks below. |
| Mask-thumbnail hover — fixed | `BuildMaskPreviewTooltip` passes the existing pooled thumbnail to `SMixtormatHelp::MakeStyledToolTip`'s optional preview slot. | Shared help panel, padding, body text and width replace its private `SToolTip` surface. Thumbnail size follows Gallery TileSize, bounded by the help content width; texture lookup and thumbnail lifetime are preserved. |
| Interactive popovers — fixed/checked | Parameter info/range/authoring, driver/group controls and preset naming reuse `MixtormatParameterPresentation` for bounded, clipped, themed scrolling. | Menu/card surfaces remain outside the scroll viewport. Form/info rows can grow beyond RowHeight; captions, errors and action labels wrap. Existing callbacks, type locks, enabled states and commit/dismiss routes are retained; runtime interaction checks remain. |

### Tooltip trace — current source, not a match-count certification

- `SMixtormatGroupAction` consumes inherited `.ToolTipText` into `SMixtormatHelp` and
  clears the native tooltip. The shell-action wrapper uses this path: Undo/Redo and
  the other top-bar actions are **not** violations. Layer creation group actions also qualify.
- `SMixtormatIconButton` styles its own `.ToolTip(FText)`, **not** inherited `.ToolTipText`.
  Inspector-group reset now uses the correct argument. `SMixtormatLayerIcon` has no
  such text wrapper; child/group visibility, disclosure and inspector preview callers
  now use the shared `IToolTip` adapter instead.
- `MixtormatMenuBuilder` previously forwarded help to `SMixtormatMenuItem`'s native
  inherited argument. It now supplies the styled adapter; entries without help retain no tooltip.
- Migrated native paths: Tile's `SetToolTipText`, instance-banner buttons, Peel seed-mask
  clearing, height inversion, ID texture/swatch/legacy container, fuzz/fill swatches,
  library ownership badge, Create Material and the mask-bar heading.
- The shared help host's empty `SetToolTipText` calls deliberately clear native help.
  Its `SNew(SToolTip)` is the themed adapter, not an unstyled-tooltip violation.
- **Completed by Q/context owner (source only):** `SMixtormat_Preview.cpp` now uses
  `SMixtormatIconButton.ToolTip(FText)` in `MakeFeaturePreviewButton` and
  `MakeChildOutputPreviewButton`. `MixtormatLayerMenus.cpp::BuildGroupAccentMenu` uses
  `SMixtormatHelp::MakeStyledToolTip` on each swatch `SBox`. Existing visuals/click handlers
  are unchanged; hover timing/placement and focus/capture still need owner runtime checks.
  Related context guidance is documented in `workspace-layout/context-help-q-plan.md`;
  no Rust/Moss/GPU implementation was changed.
- `MixtormatEditorModule.cpp`'s tab-spawner `SetTooltipText` is Unreal host registration,
  not a Mixtormat widget wrapper. Its native tab/menu presentation remains an explicit
  integration exception; no unsupported adapter API was assumed.
- Native Content Browser/object pickers and bake `FMenuBuilder` entries retain engine-owned
  presentation/help. Exact integration boundaries and owner checks are recorded below;
  these are exceptions, not evidence that every engine-owned popup is themed.

### Popover sizing and remaining owner-run checks

- Info/range/authoring keep their existing resolved content widths (260/300/320 defaults).
  Driver/group keep 224/360 structural widths, existing card/menu surfaces and text-field styles.
  All use the existing 600px `ParameterPopoverMaxHeight` and shared scrollbar metrics.
- Preset naming is bounded to the existing `MenuLayout.Width`, not only a minimum;
  heading/instructions use shared Caption styling. Error text uses palette Error and
  wraps instead of inheriting the row-label ellipsis policy.
- Group actions use a minimum height rather than a fixed box. Disabled enum creation,
  locked type controls, validation and label-based commit gating remain intact.
- Owner: compile in UE 5.8; inspect default/custom themes, large fonts, narrow windows,
  long unbroken names and DPI scaling. Check scroll reachability and popup edge placement.
- Owner: verify ramp capture/drag/crossing, picker focus, text Enter/Tab/Escape, right-click
  cancellation, nested menus, click-away dismissal, disabled help and theme reconstruction.
- Owner: compare thumbnail rendering/lifetime, hover placement and timing. The hover host
  still uses `HelpDelay`; `MakeStyledToolTip` retains Slate's native tooltip timing/lifecycle.
- Remaining bake glyph/mapping/validation literals are now tokenized as itemized below.
  Completed typography, summary leading, scrolling and thumbnail-help fixes were not redone.

No commands, builds, tests, diagnostics or automated validation were run. Findings above
are from targeted source inspection and edits only; visual/interaction equivalence is not certified.
Action/result dialog sizes already use tokens/theme; do not rewrite them just for using `SWindow`.
Keep data colors/ranges, identity multipliers and retained window geometry out of style cleanup.

The older inventory below remains historical; the current section supersedes its ramp/tab rows.

### Bake follow-up — source changes, 2026-10-24

Scope: `Source/MixtormatEditor/Private/Widgets/Dialogs/SMixtormatBakeSettingsDialog.cpp`
and the bake-only block of `Source/MixtormatEditor/Private/Style/MixtormatDesignTokens.h`.
With owner approval, added five structural `constexpr float` tokens and replaced the
corresponding dialog literals without changing values. No unrelated icon roles were
borrowed; shared Q/drag and other owners' settings remain untouched. These are structural
tokens alongside the existing bake layout tokens, not new live UI STYLE controls.

| Completed site | Current token in `MixtormatTokens` | Preserved presentation |
|---|---|---|
| `Construct`, Browse folder glyph | `BakeDialogBrowseGlyphSize` | Width/height 12×12; `MixtormatIcons::Folder()` and palette Text unchanged. |
| `Construct`, `TextureMappingsCaption` slot | `BakeDialogMappingCaptionGap` | Bottom padding 6px. |
| `Construct`, validation slot | `BakeDialogValidationTopMargin` / `BakeDialogValidationBottomMargin` | Top/bottom padding 6px/4px, including when the text is empty. |
| `BuildParameterMappingDropdown`, label slot | `BakeDialogMappingLabelGap` | Right padding 8px; existing 90px label-width token unchanged. |

Existing bake field/section/browse gaps and footer action margin already use structural
`MixtormatTokens`; dialog padding/button gap and path-picker dimensions read resolved
metrics. They are not outstanding local-literal fixes. All callbacks, mapping
identity/association, automatic matching, validation clearing and Bake/Cancel behavior
remain untouched. Native popup/help exceptions below remain; no runtime equivalence
is claimed for these source-only styling changes.

### Bake result follow-up — local styling only

Current-source inspection confirmed the five settings-dialog tokens above and their readers
already exist; this pass did not redo them or edit shared tokens/theme/schema. The additional
source change is confined to `Widgets/Dialogs/SMixtormatBakeResultDialog.cpp`:

- `Construct`: reuse the settings dialog's window brush and scrollbar style/thickness;
  message text uses resolved Body typography and palette Text instead of native defaults.
- `MakeActionButton`: reuse TopButton and ControlLabel/Text styling. Close, Re-bake,
  Apply to Selected Actors, Open Material Instance and Reveal Outputs keep their exact
  labels, order, action values, callbacks and window-close path. No enabled-state policy,
  tooltip, input wrapper, scroll container, focus or capture handler was added or removed.
- Existing dialog dimensions, padding, button gaps, footer margin and message AutoWrapText
  remain. The horizontal footer is intentionally not redesigned; large-font/localized-label
  fit remains an owner check, not a claim of responsive-layout certification.

Owner checks for **Bake Complete**: compare default/custom themes and DPI/font scales;
inspect body contrast, button normal/hover/pressed/keyboard-focus states and scrollbar
appearance. With long output reports, drag/wheel-scroll to the final line; confirm all five
footer actions stay visible and reachable. Verify Tab/Shift+Tab traversal, button activation,
modal focus restoration and each action's existing result. Check title-bar close separately.
No runtime/Unreal verification was performed; no commands, builds, tests or diagnostics ran.
Q/drag and all excluded feature-owner files remain untouched.

### Engine-owned popup/help exceptions — retained, not restyled

All bake sites below are in `SMixtormatBakeSettingsDialog.cpp`. Engine implementation/API
availability was not inspected; these are confirmed native call-site boundaries, not
claims that Unreal cannot support customization. This follow-up rechecked the listed call
sites against current source; their native integrations are not unfinished replacement work.
No widget-tree tooltip suppression,
replacement picker or new wrapper is proposed. `SMixtormatHelp` clears native help in
its subtree, so wrapping these controls could erase engine-generated descriptions.

| Exact site | Retained integration boundary | Precise owner visual/interaction check |
|---|---|---|
| `BuildPathPicker` → `CreatePathPicker(Config)` | Content Browser owns tree, search, scrolling and internal help; outer width/height already use `BakeDialogPathPickerWidth/Height`. | Open Browse at a screen edge; inspect search/tree help, long paths, scrollbar reachability and keyboard focus. Select a folder: destination updates and menus dismiss. Check Escape/click-away without selection. |
| `Construct` → `SObjectPropertyEntryBox` | Engine owns asset picker/help, selection and browse/use/clear affordances; `AllowedClass(UMaterialInterface)`, `AllowClear(true)` and `DisplayThumbnail(false)` stay unchanged. | In Custom Material mode, select and clear a master; inspect asset help, search focus and scrolling. Confirm mapping refresh and validation clearing. Keep the entry thumbnail hidden; retain picker thumbnails wherever the engine supplies them. |
| `BuildParameterMenu` → `FMenuBuilder` / `AddMenuEntry` | Native radio entries retain `[Unassigned]` help and parameter/sampler descriptions. `FUIAction` owns execution/checked state; no per-entry `IToolTip` adapter is used at this call site. | Open all four mappings with zero/many parameters and long global/layer/blend names. Inspect native help, radio checks, keyboard selection, scrolling, popup edges and dismissal. Choose `[Unassigned]` and a parameter; confirm the displayed label and validation clearing. No disabled predicate/reason exists in these entries; do not invent one. |
| `MixtormatEditorModule.cpp::StartupModule` → tab-spawner `SetTooltipText` | Unreal host registration owns tab/menu help; spawner remains `Hidden`. | Inspect workspace tab/host help where exposed; retain host activation/focus and do not expose a new menu entry just to check styling. |
| `SMixtormatBakeResultDialog.cpp::ShowMixtormatBakeResultDialog` → `SWindow` / `AddModalWindow` | Native title bar, window controls and modal ownership remain engine-managed; only the authored result content is restyled. | Close via title bar and footer, check parent-window focus restoration, and inspect window controls at high DPI. Do not replace the modal window to theme host chrome. |

Owner acceptance, still pending: default/custom themes, large fonts and DPI scaling;
Browse glyph size; mapping caption/label spacing; validation above the footer with empty
and long errors; long output-summary scrolling with Bake/Cancel reachable. Check native
popup focus, capture release and disabled explanations wherever the engine supplies them.
These checks do not reopen completed Q/drag work. No runtime verification was performed.

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
| Sizing | `Icons.Roles[].GlyphSize` / `ButtonSize`, `LayerLayout.RowHeight` / `ThumbnailSize`, `PreviewLayout.ResolutionControlWidth`, `MenuLayout.Width` / `LipHeight` / `RowHeight`, `SegmentHeight`, `ScalarRampHeight`, `ColorRampHeight`, `ControlLayout.*Dialog*`, `ControlLayout.WelcomeCard*`, `MixtormatTokens::InspectorWidth` |
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
