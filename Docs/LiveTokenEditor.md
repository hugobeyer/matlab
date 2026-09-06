# Live token editing — feasibility

Two options were asked about. Short version: **A is a rewrite and doesn't give you what you want.
B is roughly 2–3 days and lands well.**

---

## Option A — port the UI to UMG / the widget visual editor

**Verdict: don't.**

It is not a conversion, it is a rewrite of ~20k lines, and at the end you would have a worse
version of the tweaking loop than option B gives you.

The reason is what the UMG designer *is*. It edits **one instance** — nudge this button 2px in this
layout. A design system needs the opposite operation: change every button at once. Tweaking
`ButtonHeight` in a UMG hierarchy means finding every button and editing each one, which is exactly
the problem the token header already solved.

What would also not survive the port:

| Feature | Why it doesn't port |
|---|---|
| `SMixtormatSlider` — drag-to-scrub, shift-fine, click-to-type | Custom `OnPaint` + mouse capture; no UMG equivalent |
| `SMixtormatGradientBox`, `MixtormatGradientPainter` | Hand-emitted vertex spans (`GradientSamplesPerSpan = 12`) |
| `SMixtormatMenuPanel` painted lip/tint | Custom `OnPaint` |
| `FAssetThumbnail` / `FAssetThumbnailPool` | Editor-only Slate API, not exposed to UMG |
| `MixtormatDragDropOps` drag ghosts | `FDragDropOperation` is Slate-level |
| Nomad tab docking | `SDockTab` is Slate; Editor Utility Widgets dock differently |

Editor Utility Widgets exist and would technically host it, but you would be reimplementing every
custom-painted control on top of `UWidget` wrappers first.

---

## Option B — live token panel writing JSON

**Verdict: do this. ~2–3 days.** The codebase is unusually well set up for it, because of the
audit work that already happened.

### Why it's cheap: the call sites don't have to change

- `MixtormatTokens::RowHeight` is `constexpr float`. Change it to a **non-const `inline float`**
  and all **534 call sites keep compiling untouched** — it stays a plain identifier, not a
  function call. That is the whole trick.
- `MixtormatPalette::Accent()` is already an `inline` function. Its **147 call sites** need no
  change at all; only the body changes, to read a table instead of returning a literal.

So the "make it runtime-editable" step is two header edits, not a 700-site refactor.

### Two tiers of "apply", not three

| Tier | What | Apply cost |
|---|---|---|
| 1 | Palette colours read inside `OnPaint` — `SMixtormatSlider`, `SMixtormatGradientBox`, `SMixtormatMenuPanel` | **Instant.** Next paint, nothing rebuilt |
| 2 | Everything else — brush colours *and* token lengths | Stash state → close tab → style re-init → reopen |

There is no middle tier where colours update without a tree rebuild. **71 sites take the address
of a style object** — `.TextStyle(&Style.GetWidgetStyle<FTextBlockStyle>(...))`,
`.ButtonStyle(&...)`, `.Style(&...)` across the atoms, rows, layers and menus. Those are raw
pointers into the style set, so calling `FMixtormatStyle::Shutdown()` while the tree is alive
dangles all of them and crashes on the next paint.

The tab must come down **before** the style is re-registered. Same for token lengths, which Slate
copies into `FMargin` slot values at construct.

The practical consequence: **the state stash is not optional, even for a colours-only v1.**
Everything that isn't a painted gradient goes through the respawn.

### The rebuild path is clean

The tool is spawned whole from `SNew(SMixtormat)` in a nomad tab
([MixtormatEditorModule.cpp](../Source/MixtormatEditor/Private/MixtormatEditorModule.cpp)), so
"apply" is one ordered sequence: **stash → close tab → style `Shutdown`/`Initialize` → reopen →
restore.** No per-widget invalidation plumbing.

Nothing survives that boundary to go stale: `MixtormatIcons::Get()` calls `GetBrush()` fresh every
time, and `FSlateIcon` stores names rather than pointers.

**State survives cheaply.** The document is in `WorkingMaterialAsset`
(`TStrongObjectPtr<UMixtormatMaterial>`), the stack in `WorkingLayers`, and `FEditHistoryState` is
just `TArray<FMixtormatLayer>` — all plainly copyable. Stash them module-side before teardown,
restore in `Construct`.

### One real hazard, worth designing around up front

`constexpr float TabHeight = ButtonHeight;` — there are **10 derived tokens** like this
(`CornerRadiusInner`, `TabHeight`, `TabUnderlineThickness`, `SegmentHeight`, `MenuItemHeight`,
`MenuLipHeight`, `ToggleFillSize`, `LayerEdgeWidth`, `FontLayerName`, `FontSliderLabel`).

Turned into non-const `inline` variables, those become **dynamic initialization, and inline
variables initialise in unordered fashion across translation units.** `TabHeight` could come up as
0 depending on link order — an intermittent, build-dependent bug that would be miserable to chase.

Fix is cheap but has to be in the design from the start: every token gets a **literal**
initialiser, and all derivations move into an explicit `RecomputeDerived()` called from
`FMixtormatStyle::Initialize()`.

### Recommended mechanism: one X-macro list

Declare tokens once:

```
MIXTORMAT_TOKEN(RowHeight, 18.0f)
MIXTORMAT_COLOR(Accent, 0x4D8FA8, 1.0f)
```

expanding to both the variable **and** a `{ name → float* }` / `{ name → colour* }` table. That
table is what the tweak panel, the JSON writer and the JSON reader all consume — one list instead
of three that drift apart. Adding a token from the audit becomes one line.

Side benefit: it puts the duplicate hexes from the audit literally next to each other in one file,
where `0x383C3E` vs `0x383D41` is obvious.

**JSON should be an override layer**, read at `Initialize()` with the header literals as fallback.
A missing or malformed file degrades to exactly today's behaviour — and the header keeps its
rationale comments, which are the best documentation in this codebase and should not migrate into
a JSON blob.

### The panel itself is mostly assembly

`SMixtormatSlider`, `SMixtormatRow`, `AddSliderRow` and `SMixtormatInspectorGroup` already exist
and already do numeric rows with drag-scrub and reset. The panel is a scroll box of them, grouped
by the token header's own section comments, plus Save / Reload / Revert.

---

## What you actually asked to tweak

Paddings, margins, alignments, font sizes, boldness. Those are **not** one job — they range from
free to not-worth-doing.

| Ask | State today | Cost |
|---|---|---|
| **Font sizes** | Already tokenized — `FontBody`, `FontCaption`, `FontGroupHeader`, `FontSliderLabel`, `FontTile`, `FontBadge`, `FontLayerSource` | **Free.** Rides the `inline float` change |
| **Boldness** | Not tokenized, but the surface is small — see below | **Cheap.** ~0.25 day |
| **Paddings / margins** | 136 sites token-driven, **65 still literal** | **The main job.** Needs the audit fixes first |
| **Alignments** | 140 sites, none tokenized, 93 of them `VAlign_Center` | **Mostly don't.** See below |

In Slate, padding and margin are the same thing — `FMargin` on a slot. One knob type, not two.

### Boldness: do it at the 14 text styles, not the 26 call sites

`MixtormatStyle.cpp` registers **14 `FTextBlockStyle`s** (`SectionHeader`, `RowLabel`, `LayerName`,
`MenuLabel`, `BadgeText`, …). Expose those as **(face, size, letter-spacing)** tuples and 14 rows
give you weight *and* size *and* tracking for the whole tool.

They're rebuilt by `Initialize()`, so they ride the respawn path already designed — no new
plumbing.

The 26 inline `GetDefaultFontStyle(TEXT("Bold"), …)` calls are the ones *bypassing* the style set.
Those are audit items — fold each into a text style — not panel rows.

### Alignments: not worth it as a knob

Of 140 sites, **93 are `VAlign_Center`**. That is structural, not designed — a knob that
un-centers a label inside its row is a knob nobody reaches for, and 140 enum dropdowns would bury
the values you do want.

The alignments that *are* design decisions all live in the shared builders — `SMixtormatRow`,
`AddSliderRow`, `SMixtormatInspectorGroup`, `SMixtormatLayerRow`. **Offer that handful** (roughly
6–10 rows) and leave the rest structural.

---

## Revised order — this changes the sequencing

Lengths are now the first thing, not the last. Two consequences:

**The audit's padding fixes become a prerequisite, not a follow-on.** 65 literal padding sites are
65 knobs that don't exist in the panel. You would drag nothing and conclude the tool is broken.
This reverses the earlier "build the editor first" note — that holds for *discovering* missing
tokens in general, but not when paddings are the thing you came to tweak.

**The derived-token hazard lands on day one**, not day three, because lengths are where it lives.

Colours drop to last — you didn't ask for them, and they're additive once the machinery exists.

| Step | Cost |
|---|---|
| 1. Land the audit's padding fixes — the 65 literal sites, live UI only | ~0.5–1 day |
| 2. Tokens → `inline float` + X-macro list + `RecomputeDerived()` | ~0.5 day |
| 3. The 14 text styles into the same table (face + size + tracking) | ~0.25 day |
| 4. JSON read/write + the panel | ~0.5 day |
| 5. Stash/restore + respawn apply | ~0.5 day |
| 6. *(later, optional)* colours — additive, no new machinery | ~0.5 day |

**~2.5–3 days** to the point where paddings, margins, font sizes and weights are all live.

Keep the audit's own exclusions in step 1 — the placeholder Shell tabs stay untouched.

### Confirmed: nothing has to stay `constexpr`

`BadgeMaxCharacters`, `GradientSamplesPerSpan`, `MaskPickerColumns` and `PreviewHdriPresetLimit`
were the risk — an array bound would force one to stay `constexpr` and split the model. Checked:
all four are runtime values (`BadgeMaxCharacters` is referenced only in a comment). The whole
header can go `inline`.
