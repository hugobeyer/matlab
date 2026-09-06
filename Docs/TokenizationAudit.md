# Tokenization audit — repeated hardcoded values

Scope: `Source/MixtormatEditor/Private/{Style,UI,Widgets}`. Only values that are **repeated,
duplicated, or drifting**.

**Re-audited after the last round of edits.** Status column reflects current state.
`UI/**` remains clean — the literals are in `Widgets/**` and `Style/MixtormatStyle.cpp`.

## Churn since the first pass

| File | Then | Now |
|---|---|---|
| `SMixtormat_Inspector.cpp` | 2854 | **3033** |
| `SMixtormat_Layers.cpp` | 2540 | **2425** |
| `SMixtormatInternal.h` | 914 | 921 |
| `MixtormatDesignTokens.h` | 354 | 357 |
| `MixtormatStyle.cpp` | 671 | 671 (untouched) |

Token refs 534 → 531, palette refs 147 → 147, alignment sites 140 → 137,
`GetDefaultFontStyle` 26 → 26.

**Net: one item fixed, three new ones.** The style file was not touched at all, so every
style-side finding stands.

---

## NEW — 14 orphaned tokens

Defined in `MixtormatDesignTokens.h`, referenced **nowhere**. The reverse of the original problem:
not a literal without a token, but a token without a call site. Each is either a feature that got
reworked and left its token behind, or a value that quietly moved elsewhere.

```
RowGapTight              ButtonPressedOffset      MaskPickerColumns
ColorSwatchWidth         GroupBodyTopInset        MaskPickerColumnsDense
ColorSwatchHeight        SegmentShadeAlpha        DropLineThickness
ColorSwatchPadding       SurfaceTileSizeDense     TileBadgeHeight
                         MaskPickerTileSizeDense  MaterialGalleryHeaderGap
```

Two worth calling out:

- **`ColorSwatchWidth/Height/Padding`** were added *during* the first audit to fix the
  `FVector2D(76,16)` swatches — then the swatches were reworked to be size-free or to use
  `LayerThumbnailSize`, and the new tokens were never wired up. Born orphaned.
- **`SegmentShadeAlpha = 0.1f`** is dead because the value moved into
  `MixtormatPalette::SegmentShade() = Hex(0x000000, 0.10f)`. The `0.10` now lives in two places,
  one of which nothing reads.

These matter more than usual given the live-tweaker plan: **an orphaned token is a knob in the
panel that visibly does nothing.** Either wire each up or delete it.

## NEW — orphaned palette colour, and a hover state that never lightens

`MixtormatPalette::WellTopHover()` has no callers.

`WellTop`/`WellBottom` are used as a **gradient pair** by `SMixtormatChip`, `SMixtormatToggle` and
`SMixtormatSegmentedControl`. `WellTopHover`/`WellBottomHover` were evidently the hover pair — but
only `WellBottomHover` is referenced, twice, as a flat brush colour in `MixtormatStyle.cpp:368,371`.

So those gradient wells have **no lightened hover gradient**; the top stop is dead. Probably a
dropped hover state rather than a naming slip. Worth a look before deciding which way to fix it.

---

## 1. Root cause: two competing layout namespaces — UNCHANGED

`Widgets/SMixtormatInternal.h:99` still declares `MixtormatUI` alongside `MixtormatTokens`:

| Constant | Value | Used in |
|---|---|---|
| `PanelPadding` | 4.0f | Shell, Library, Layers |
| `SplitterHandleSize` / `SplitterHitSize` | 1.0f / 5.0f | Shell, Library |
| `LayerStackWidth` | 240.0f | Layers |
| `InspectorWidth` | 300.0f | Inspector, Preview (×3) |
| `TopBarHeight` / `StatusBarHeight` | 32.0f / 18.0f | Shell |
| `MaskTileSize` | 62.0f | Layers (×2) |

**Name collision stands:** `MixtormatUI::MaskTileSize = 62.0f` vs
`MixtormatTokens::MaskTileSize = 96.0f`. Two sizes, one name, resolved by which header a file
includes. Still the root cause of the rest.

## 2. Tokens that exist but are not applied

| Literal | Then | Now | Where |
|---|---|---|---|
| `FVector2D(76, 16)` colour swatch | 3 | **0 — fixed** | reworked; left `ColorSwatch*` orphaned |
| `FVector2D(108, 18)` colour swatch | 1 | 1 | Inspector:2904 |
| `GetDefaultFontStyle("Bold", 8)` → `FontCaption` | 4 | 4 | Inspector:2521, 2533; Preview:282, 317 |
| `GetDefaultFontStyle("Bold", 9)` | 4 | **6** | Internal.h:314, 343, 371; Layers:1790; **DragDropOps.h:83, 237** |
| Outline `1.0f` → `OutlineWidth` | 12 | 12 | MixtormatStyle.cpp |
| Corner radius `2/4/6/7` vs `CornerRadius = 3` | 5 | 5 | MixtormatStyle.cpp:103, 112, 118, 121, 169 |

## 3. Duplicate / near-duplicate palette colours — UNCHANGED

| Hex | Names sharing it |
|---|---|
| `0x4D8FA8` | `Accent`, `FocusFill`, `SelectionFill`, `MenuTint` |
| `0x070808` | `WellTop`, `WellEntry`, `OverlayPlateTop` |
| `0x101112` | `Inset`, `ThumbnailBackground`, `LayerHiddenEnd` |
| `0x0C0E0F` | `WellBottom`, `OverlayPlateBottom` |
| `0x242729` | `WellOutline`, `Divider` |
| `0x383D41` | `FillTopHover`, `WellOutlineHover` |
| `0x25282B` | `HeaderTint`, `HeaderTintFade` |
| `0x24282B` | `FillBottom`, `FillDisabled` |
| `0x191B1D` | `Panel`, `LayerHiddenTop` |
| `0xA8A8A8` | `HeaderText`, `LayerSource` |

**Still the sharpest finding:** `BorderStrong = 0x383C3E` (line 44) vs
`WellOutlineHover = 0x383D41` (line 79) and `FillTopHover = 0x383D41` (line 85). One digit apart,
same visual role.

## 4. Outline-alpha drift — UNCHANGED

`0.2 · 0.25 · 0.3 · 0.35 · 0.4 · 0.42 · 0.45 · 0.5 · 0.55 · 0.65 · 0.8` — eleven values for what
is really three states. **One** item: a small alpha scale, not eleven tokens.

## 5. Repeated layout literals with no token

| Literal | Then | Now | Where |
|---|---|---|---|
| `Padding(6, 0, 0, 0)` | 7 | 7 | SMixtormatInternal.h:400, 520, 527, 669, 673, 677, 681 |
| `Padding(5, 0)` | 5 | **4** | Shell |
| `Padding(2, 0)` | 4 | 4 | Shell:97, 119, 142, 174 |
| `Padding(12)` | 3 | 3 | SMixtormatInternal.h:306, 497, 648 |
| `Padding(0, 0, 3, 0)` | 3 | 3 | Inspector |
| `MaxHeight(420)` | 3 | 3 | Inspector |
| `Padding(4, 0, 0, 0)` | 3 | 3 | Library, Preview, Internal.h |
| `Padding(0, 4, 0, 10)` | 2 | 2 | Internal.h:316, 345 |
| `Padding(0, 0, 0, 8)` | 2 | 2 | Internal.h:355, 384 |
| `Padding(0, 10, 0, 0)` | 2 | 2 | Internal.h:511, 662 |
| `ClientSize(560, 320)` | 2 | 2 | Internal.h:583, 610 |
| `FAssetThumbnail(…, 40, 40, …)` | 3 | 3 | MixtormatDragDropOps.h:57, 58, 128 |

## 6. Style-file paddings bypassing the button tokens — UNCHANGED

- `MixtormatStyle.cpp:182` `FMargin(2.0f)` / `:183` `(2, 3, 2, 1)`
- `:323` `(4.0f, 1.0f)` / `:324` `(4, 2, 4, 0)`
- `:265` `SetPadding(FMargin(2.0f))`
- `:334` `SetTextPadding(FMargin(3.0f, 0.0f))`

The `(x, y)` → `(x, y+1, x, y−1)` pressed-offset pattern repeats and is exactly what
`ButtonPressedOffset` is for — which is itself one of the 14 orphans above.

---

## Headline number

**Padding sites: 136 token-driven, 65 still literal.** Unchanged.

That ratio is the one to watch for the live tweaker — those 65 are knobs that will not exist in
the panel.

## Deliberately excluded

- **Interaction values, per the header's own rule:** `FineDragScale`, `DragThreshold`, one-pixel
  hairlines, `FillWidth(1.0f)` weights, zero margins.
- **3D / camera constants** in `SMixtormatPreviewViewport.cpp` (`-35.0f` light pitch, `360.0f`
  wrap, `0.35f` yaw rate).
- **Placeholder UI** in `SMixtormat_Shell.cpp:380–413` — unbuilt screens; tokenizing them locks in
  numbers nobody chose.
- **Resolution constants** (`1024/2048/4096`, Preview.cpp) — data, not layout.
- **Alignment sites** (137) — 93 are `VAlign_Center`, structural rather than designed. See
  [LiveTokenEditor.md](LiveTokenEditor.md).
