# Tokenization audit — repeated hardcoded values

Scope: `Source/MixtormatEditor/Private/{Style,UI,Widgets}`. Only values that are **repeated,
duplicated, or drifting** — one-off numbers are left out deliberately.

Snapshot note: `MixtormatDesignTokens.h` was edited on disk mid-audit (the `ColorSwatch*` tokens
appeared). This list reflects the file as it stands now.

Health: `UI/**` is effectively clean — the literals are all in `Widgets/**` and `Style/MixtormatStyle.cpp`.

---

## 1. Root cause: two competing layout namespaces

`Widgets/SMixtormatInternal.h:99` declares a second constant namespace, `MixtormatUI`, alongside
`MixtormatTokens`:

| `MixtormatUI` constant | Value | Used in |
|---|---|---|
| `PanelPadding` | 4.0f | Shell, Library, Layers |
| `SplitterHandleSize` / `SplitterHitSize` | 1.0f / 5.0f | Shell, Library |
| `LayerStackWidth` | 240.0f | Layers |
| `InspectorWidth` | 300.0f | Inspector, Preview (×3) |
| `TopBarHeight` / `StatusBarHeight` | 32.0f / 18.0f | Shell |
| `MaskTileSize` | 62.0f | Layers (×2) |

**Name collision:** `MixtormatUI::MaskTileSize = 62.0f` vs `MixtormatTokens::MaskTileSize = 96.0f`.
Two different sizes under one name, resolved by which header a file happens to include.
Everything below is downstream of this split — these six belong in `MixtormatTokens`.

---

## 2. Tokens that already exist but are not applied

| Literal | Count | Where | Existing token |
|---|---|---|---|
| `FVector2D(76.0f, 16.0f)` on `SColorBlock` | 3 | Inspector:539, 1448, 2467 | `ColorSwatchWidth` / `ColorSwatchHeight` (added, still unused) |
| `FVector2D(108.0f, 18.0f)` — the fill-colour swatch | 1 | Inspector:2725 | same family, wide variant — needs one more token, not a literal |
| `GetDefaultFontStyle("Bold", 8)` | 4 | Inspector:2282, 2294; Preview:282, 317 | `FontCaption = 8.0f` |
| Outline width `1.0f` in `FSlateRoundedBoxBrush` | 12 | MixtormatStyle.cpp | `OutlineWidth` |
| Corner radius `2.0f` / `4.0f` / `6.0f` / `7.0f` | 5 | MixtormatStyle.cpp:103,112,118,121,169 | `CornerRadius = 3.0f` exists and none of them use it |

---

## 3. Duplicate / near-duplicate colours in the palette

Same hex under multiple semantic names — fine as aliases, but they are written as independent
literals, so retuning one silently desyncs the set:

| Hex | Names sharing it |
|---|---|
| `0x4D8FA8` | `Accent`, `FocusFill`, `SelectionFill`, `MenuTint` (4×) |
| `0x070808` | `WellTop`, `WellEntry`, `OverlayPlateTop` |
| `0x101112` | `Inset`, `ThumbnailBackground`, `LayerHiddenEnd` |
| `0x0C0E0F` | `WellBottom`, `OverlayPlateBottom` |
| `0x242729` | `WellOutline`, `Divider` |
| `0x383D41` | `FillTopHover`, `WellOutlineHover` |
| `0x25282B` | `HeaderTint`, `HeaderTintFade` |
| `0x24282B` | `FillBottom`, `FillDisabled` |
| `0x191B1D` | `Panel`, `LayerHiddenTop` |
| `0xA8A8A8` | `HeaderText`, `LayerSource` |

**Drift, not aliasing — the strongest finding here:** `BorderStrong = 0x383C3E` vs
`WellOutlineHover / FillTopHover = 0x383D41`. One digit apart, same visual role. Almost certainly
meant to be one value.

## 4. Outline-alpha drift in the style file

`FSlateRoundedBoxBrush` outline alphas across `MixtormatStyle.cpp`, all hand-picked:

`0.2 · 0.25 · 0.3 · 0.35 · 0.4 · 0.42 · 0.45 · 0.5 · 0.55 · 0.65 · 0.8`

Eleven distinct values for what is really three states (rest / hover / focus). Treat as **one**
item: a small alpha scale, not eleven tokens.

---

## 5. Repeated layout literals with no token

Ordered by how often they repeat.

| Literal | Count | Where | Role |
|---|---|---|---|
| `Padding(6.0f, 0, 0, 0)` | 7 | SMixtormatInternal.h:400, 520, 527, 669, 673, 677, 681 | gap between dialog footer buttons |
| `Padding(5.0f, 0.0f)` | 5 | Shell:85, 113, 136, 159, 398 | top-bar item gap |
| `Padding(2.0f, 0.0f)` | 4 | Shell:97, 119, 142, 174 | top-bar group gap |
| `Padding(12.0f)` | 3 | SMixtormatInternal.h:306, 497, 648 | modal dialog body inset |
| `Padding(0, 4.0f, 0, 10.0f)` / `(0,0,0,8.0f)` | 4 | SMixtormatInternal.h:316, 345, 373, 355, 384 | dialog section gaps |
| `Padding(0, 10.0f, 0, 0)` | 2 | SMixtormatInternal.h:511, 662 | gap above dialog footer |
| `GetDefaultFontStyle("Bold", 9)` | 4 | SMixtormatInternal.h:314, 343, 371; Layers:1955 | dialog section heading |
| `Padding(0, 0, 3.0f, 0)` | 3 | Inspector:1572, 1705, 2011 | trailing gap before inline control |
| `MaxHeight(420.0f)` | 3 | Inspector:134, 429, 1365 | scroll cap on option lists |
| `Padding(4.0f, 0, 0, 0)` | 3 | Library:289; Preview:294; SMixtormatInternal.h:329 | leading gap before trailing control |
| `FAssetThumbnail(..., 40, 40, ...)` | 3 | MixtormatDragDropOps.h:57, 58, 128 | drag-ghost thumbnail resolution |
| `ClientSize(FVector2D(560.0f, 320.0f))` | 2 | SMixtormatInternal.h:583, 610 | standard modal size |

Sibling one-offs in the same family, worth folding into whichever token covers the above rather
than left as literals: `ClientSize(760, 320)` (Internal.h:725), `ClientSize(480, 410)`
(Document.cpp:317), `WidthOverride(360)/HeightOverride(420)` (Internal.h:425), `WidthOverride(150)`
(Library:240).

---

## 6. Style-file paddings that bypass the button-padding tokens

`ButtonPaddingCompact/Primary/Tab` are used correctly for most styles, but four remain literal:

- `MixtormatStyle.cpp:182` `SetNormalPadding(FMargin(2.0f))` / `:183` `(2, 3, 2, 1)`
- `MixtormatStyle.cpp:323` `(4.0f, 1.0f)` / `:324` `(4, 2, 4, 0)`
- `MixtormatStyle.cpp:265` `SetPadding(FMargin(2.0f))`
- `MixtormatStyle.cpp:334` `SetTextPadding(FMargin(3.0f, 0.0f))`

The `(x, y)` → `(x, y+1, x, y-1)` pressed-offset pattern repeats and is what
`ButtonPressedOffset = 1.0f` exists for; neither site uses it.

---

## Deliberately excluded

- **Interaction values, per the header's own rule:** `FineDragScale`, `DragThreshold`,
  one-pixel hairlines, `FillWidth(1.0f)` / `FillHeight(1.0f)` weights, zero margins.
- **3D / camera constants** in `SMixtormatPreviewViewport.cpp` (`-35.0f` light pitch, `360.0f`
  wrap, `0.35f` yaw rate) — scene maths, not design values.
- **Placeholder UI** in `SMixtormat_Shell.cpp:380–413` (the "Saved Mixtormat looks will appear
  here" tab, `IsEnabled(false)`, fonts at 9/12/14, paddings 24/16/14/10). Unbuilt screens;
  tokenizing them would lock in numbers nobody chose.
- **Resolution constants** (`1024/2048/4096` in Preview.cpp) — data values, not layout.
