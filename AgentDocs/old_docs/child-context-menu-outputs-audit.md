# Child Context Menu / Outputs Audit

Audit of the layer-child context menus and their published-output ("Copy …")
actions, ahead of a UX redesign that moves output actions under a single
`Outputs >` submenu with `icon + semantic label` rows.

- **Repo:** `C:\Tools\MaterialLab\MatLab\Plugins\Mixtormat`
- **Verified HEAD:** `cf25a84beac660b1be5ede697744601bfff4bf49` ("Centralize child enable flag access")
- **Tree state:** clean (`main...origin/main`)
- **Status:** audit only — no code changed.

---

## 1. Which child types expose Copy / Copy as Instance / Copy Output / mask-gate-ID variants

Every child row routes through **one** shared builder, so all child types get
`Copy`, `Copy as Instance`, `Copy Output`, `Paste`, (conditional)
`Paste as Gating Mask`, `Move to Layer`, and the instance section:

| Context menu builder | Calls `AddSharedChildMenuItems` |
|---|---|
| `BuildMaskContextMenu` | `MixtormatLayerMenus.cpp` L1423 |
| `BuildEffectContextMenu` | L1185 |
| `BuildGeneratedContextMenu` | L1271 |
| `BuildBlurContextMenu` | L1352 |
| `BuildGroupChildContextMenu` | L433 |

Actual **copyable outputs** are driven by `GetChildCapabilities` +
`GetCopyableOutputs` (`bCopyableAsMask || bCopyableAsField`):

| Child type | Copyable outputs | Current flattened label |
|---|---|---|
| Filter (Cluster IDs) | RegionIds | `Copy IDs` |
| PatternId | RegionIds, Gap | `Copy IDs`, `Copy Gate · Gap` |
| OutputReference | RegionIds / Color / Flow / UVs | `Copy IDs` / `Copy Color` / `Copy Flow` / `Copy UVs` |
| IdGroup | RegionIds, Boundary | `Copy IDs`, `Copy Gate · Boundary` |
| BoundaryFromIds | Boundary, Gap, Distance | `Copy Gate · …` |
| Generator — Cracks | RegionIds + CrackMask, CrackDistance, PieceRandom, ChamferCut | `Copy IDs` + `Copy Gate · …` |
| Generator — RockFormation | RegionIds + Top/Chamfer/Wall/Signed Boundary Distance/…Ramps/Height/Slope/Gap | `Copy IDs` + `Copy Gate · …` |
| Generator — StrataCarver | RegionIds, StrataPosition, StrataRandom | `Copy IDs` + `Copy Gate · …` |
| Generator — Pebbles | RegionIds, PebbleCoverage, PebbleEdgeDistance, PebbleRandom | `Copy IDs` + `Copy Gate · …` |
| Generator — CliffStrata | RegionIds + Block Seam/Row Seam/Cavity/Raw Voronoi/Coverage | `Copy IDs` + `Copy Gate · …` |
| Effect — generator-flow (ShapeDeform/GeneratorFlow/FlowCarve) | FlowDirection, WarpedUV | `Copy Flow`, `Copy UVs` |
| Effect — Breakup | RegionIds, Gap, Edge, Pieces | `Copy IDs` + `Copy Gate · …` |
| Effect — WornEdges | Wear | `Copy Gate · Wear` |
| HeightColorRamp | Color | `Copy Color` |
| Mask / Generated / Craquelure / ColorId / RandomId / Blur / Curvature / RampId | none | submenu disabled, no rows |

Notes:
- `RampId`'s Ramp is preview-only (two-channel, not a scalar mask) — not copyable.
- Generator-flow `Influence` / `Validity` / `CarveMask` are preview-only — not copyable.

---

## 2. Functions that build those menu items

- `SMixtormat::AddSharedChildMenuItems` — `MixtormatLayerMenus.cpp` **L211–339**.
  The single source for Copy / Copy as Instance / Copy Output / flattened output
  rows / Paste / Paste as Gating Mask / Move to Layer / instance section.
- `SMixtormat::BuildCopyChildOutputMenu` — **L189–209**. The `Copy Output`
  submenu content (same outputs; icon `Mask()` vs `Generated()`).
- `SMixtormat::AddIdGroupMenuItems` — **L877–903**. IdGroup-only structural
  `Add Source` / `Add IDs` / `Add From IDs`.
- `SMixtormat::AddGeneratorFlowMenuItems` — **L1682–1695**. Structural creation
  of ShapeDeform / GeneratorFlow / FlowCarve effect children.
- Data source: `GetChildCapabilities` / `GetCopyableOutputs` —
  `MixtormatChildCapabilities.cpp` L35–343.
- Actions: `CopyChildOutput` / `CanCopyChildOutput` (clipboard file).

---

## 3. Structural vs published-output actions

**Structural (child lifecycle / ownership):**
Copy, Copy as Instance, Paste, Paste as Gating Mask, Move to Layer, Duplicate,
Remove/Delete, Go to Source, Break Instance, Replace Source, Copy Instance
Reference, Add Gating Mask, Add Flow Warp, Blend Mode, Add Blur/Curvature,
Add Source/IDs/From IDs, generator-flow adds.

**Published-output (data lift):**
The `Copy Output` submenu **and** the flattened `Copy Gate · X` / `Copy IDs` /
`Copy Flow` / `Copy UVs` / `Copy Color` rows.

**Key finding — the overload is literal duplication.**
`AddSharedChildMenuItems` builds a `Copy Output` submenu at **L237–239** *and
then* the same outputs again as flattened rows at **L240–264**. Both are live.
That duplication is the source of the visual bloat.

---

## 4. Existing icon support

`MixtormatIcons.h` exposes: `Mask()`, `Effect()`, `Generator()`, `Generated()`,
`Ids()`, `Duplicate()`, `Trash()`, `Eye/EyeOff`, `ArrowUp/Down`, `Add`,
`Check`, `Folder`, `LayerMaterial`, `LayerFill`, `ScalarRamp*`.

| Semantic output | Icon today |
|---|---|
| Mask | `Mask()` ✅ |
| Region IDs | `Ids()` ✅ |
| Gate | none dedicated — currently `Mask()` if `bCopyableAsMask` else `Generated()` ⚠️ |
| UV | none ❌ |
| Height | none ❌ |
| Color | none ❌ (`ScalarRamp*` are ramp-editor glyphs, not a color icon) |

3 of 6 exist. UV / Height / Color / Gate need new PNGs registered in
`MixtormatStyle`.

---

## 5. Can output actions move under one `Outputs` submenu cleanly?

**Yes — presentation-only.** Clipboard/reference behavior lives entirely in
`CopyChildOutput(Address, OutputName)` / `CanCopyChildOutput`; menu rows are
just label + icon + delegate. `BuildCopyChildOutputMenu` already does exactly
this. No clipboard or reference change.

Caveat: `BuildCopyChildOutputMenu` labels rows with the raw `Output.Label`
("Gap", "Boundary"), not the semantic `Copy Gate · X` text. The label logic at
L242–252 must move into the submenu builder.

---

## 6. Shift + RMB direct open

Feasible but **not clean** with current plumbing:

- Menus open via `SMenuAnchor::SetIsOpen(true)` in each row's
  `OnMouseButtonDown` (`SMixtormatLayerChildRow.cpp` L182–193; same in
  LayerRow / GroupRow). `SMenuAnchor` opens whatever `OnGetMenuContent`
  returns — there is no per-modifier content switch.
- Would require threading `MouseEvent.IsShiftDown()` through the row → new
  delegate/arg, or a flag set before `SetIsOpen`.
- Modifier-only access is explicitly disallowed. Recommend deferring; ship the
  discoverable submenu first.

---

## Revised UX direction (locked)

- Keep `Outputs >` as one normal textual submenu. **No extra nesting**
  (no `Outputs > Region IDs > Copy`).
- One row per published output: `icon + semantic label`, e.g.
  `[IDs] Region IDs`, `[Gate] Gap`, `[Mask] Boundary`, `[Color] Color`,
  `[Flow] Flow`, `[UV] UVs`.
- Row click still calls `CopyChildOutput(Address, OutputName)` — no
  clipboard/reference semantics change.
- Drop the repetitive `Copy …` wording only. **Keep labels — never icon-only.**
- Any remaining flattened output rows get the same icon + semantic-label
  treatment rather than `Copy X`.

Mapping examples:

| Before | After |
|---|---|
| `Copy IDs` | `[IDs icon] Region IDs` |
| `Copy Gate · Gap` | `[Gate icon] Gap` |
| `Copy Color` | `[Color icon] Color` |

---

## Open questions / blockers before implementation

1. **Icons are the blocker.** Only `Ids()` and `Mask()` exist. Gate, Color,
   Flow, UV have no icon. Options: (a) add 4 new PNGs + `MixtormatStyle`
   registrations, or (b) ship with a temporary fallback
   (`Generated()` / `Mask()`) and add icons later.
2. **Flattened rows:** the duplication at `MixtormatLayerMenus.cpp` L240–264 —
   remove them (single `Outputs` submenu only), or keep them flattened *and*
   relabeled with the same icon + semantic-label treatment?
3. **Empty submenu:** currently `.Enabled(false)` when no copyable outputs but
   still occupies a row. Consider hiding entirely when empty.

---

## What would make this unsafe / awkward

- Removing the flattened rows is a **visible behavior change** — confirm first.
- `BuildCopyChildOutputMenu` and the flattened loop must not both survive the
  change.
- UV / Height / Color / Gate icons don't exist — adding them touches
  `MixtormatStyle` + PNG assets, out of scope for a menu-only change.
- Shift + RMB needs new row → `SMixtormat` plumbing; not free.
