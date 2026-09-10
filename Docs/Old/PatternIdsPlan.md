# Pattern IDs — plan and handoff

Status: **not started.** Nothing has been written. This document is the design decision plus the
complete file map, so the work can be picked up cold.

Context: the Filter/ID family already exists and works. `Cluster IDs` segments a texture into
regions from its own height and roughness; `HSV From IDs`, `Random From IDs` and `Ramp From IDs`
consume those regions. Pattern IDs is a **second producer** of the same ID map — procedural
instead of derived — so every existing consumer works on it with zero downstream change.

---

## 1. Why this is a producer, not a new family

`FindRegionIdsAbove()` (`Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp:2008`) scans a
per-layer `TArray<TPair<int32, FRDGTextureRef>> RegionIdMaps` for the nearest map published *above*
a consumer. It does not care who produced it. So a Pattern filter that emits the same
`Texture2D<uint>` and registers into that array is immediately usable by HSV, Random and Ramp.

That is the whole architectural claim, and it is what keeps this node from being five nodes.

### The ID contract — non-negotiable

`Shaders/Private/MixtormatRampIds.usf` recovers the region's anchor position by
`RootX = RootId % OutputSize.x`. So an ID **must be the linear pixel index of the region's centre**,
not `row * Columns + col`:

```hlsl
const uint Id = (uint)(CentrePixel.y * OutputSize.x + CentrePixel.x);
```

Every pattern type below has a well-defined cell centre, so all of them satisfy this. Any future
type that cannot name a centre pixel is disqualified from the family, not just from Ramp.

`MIXTORMAT_INVALID_REGION` (`0xffffffffu`, `Shaders/Private/MixtormatRegionId.ush:16`) is the
sentinel for a pixel in no region. Cluster IDs never emits it — union-find gives every pixel a
root. Pattern IDs **may**: a mortar/grout gap wide enough to be its own thing is genuinely
region-less. Consumers already guard on it, so this is safe, but it is the first node that will
actually produce it. Test that path.

---

## 2. The pattern taxonomy

The split below is **not** "two different solvers". It is one solver family — see
`MixtormatCellular.ush` at the end of this section — separated by how much work each entry needs
on top of it. Read the taxonomy for the parameter surface; read the cellular note for what the
code actually shares.

### A. Falls out of one lattice — parameters, not types

Five controls — `Rows`, `Columns`, `RowOffset`, `Jitter`, `Seed` (+ an orientation swap) — cover
everything in this table. Do **not** ship these as a mode enum.

| Pattern | Setting |
|---|---|
| Grid / tile | `RowOffset` 0 |
| Running bond, brick, marble offset | `RowOffset` 0.5 |
| Diagonal tiling | `RowOffset` = 1/`Columns` |
| Stripes | `Columns` 1 |
| Bars, vertical bars | `Rows` 1 + orientation swap |
| Stack / soldier | `RowOffset` 0 + orientation |
| Jittered cut lengths | `Jitter` on the cut positions |
| Per-row random shift | `Jitter` applied to `RowOffset` |

Precedent for the consolidation: the craquelure `Scale` control, which collapsed what were going
to be several presets into one number.

**Diagonal is a shear, not a rotation.** An arbitrary angle does not tile — the lattice meets the
UV wrap mid-cell and seams. Shifting each row by a constant `1/Columns` produces a diagonal that
closes on the period exactly. This is the same lesson `MixtormatCellular.ush` records in its header
comment about periodic-by-construction versus wrapped-input lattices, and `ErosionEffectPlan.md`
learned it the expensive way.

**Jitter must pin the cuts at k=0 and k=Columns.** Otherwise the first and last cell of a row
disagree across the seam and the pattern tears at the UV wrap.

### B. Needs its own cell solve

| Pattern | Cost | Edge distance | Ship order |
|---|---|---|---|
| **Voronoi / scattered stone** | ~free — `MixtormatCellular.ush` already does it | Exact, already computed | 1st |
| Hexagonal (triangular = its subdivision) | Small, own centre solve | Analytic | 2nd |
| Herringbone | Medium — per-cell 90° alternation | Analytic, orientation enters the distance | 3rd |
| Basketweave / parquet | Medium — bar pairs in a checker | Same | 3rd |

Two things deliberately **not** in that table:

- **Radial / concentric** — a polar UV remap feeding the list-A lattice, so rings × wedges come
  free as a UV-space toggle rather than as a sixth solver.
- **Recursive split (Mondrian)** — out of the family, not merely deferred. It needs a split tree
  walked per pixel and so has no analytic edge distance, which means it cannot satisfy the bevel /
  wear / slope contract that this whole section is ranked by. Shipping IDs for it would hand the
  user a node whose relief sliders are silently dead. If it is ever wanted it is its own node with
  its own justification, not a mode in this one.

### What ranks them: distance-to-cell-edge

Bevel, worn edges and slope all consume an edge-distance field, and those were called out as core
with "variation of these is very important". So candidates are ranked by whether they hand back a
clean edge distance, **not** by whether they can emit an ID. A type that can emit IDs but has no
usable edge distance should say so in the UI rather than ship a slider that does nothing.

### What already exists — read this before writing any cell code

`Shaders/Private/MixtormatCellular.ush` is a complete periodic cellular solver:

```hlsl
struct FMixtormatCellularResult
{
    float F1;            // distance to the nearest feature point
    float F2;            // distance to the second nearest
    float EdgeDistance;  // distance to the nearest cell boundary
    uint  CellId;        // hash of the owning cell, stable under the period
    uint  EdgeId;        // hash of the two cells sharing the boundary, order-independent
    float2 CellOffset;   // this pixel relative to its feature point
};
```

`EdgeDistance` is the perpendicular-bisector distance, so it has **even width regardless of cell
size** — which is exactly what bevel and grout need, and what `F2 - F1` does not give (it varies
with cell size, so bevels come out fat in big cells and thin in small ones). Craquelure and
chipping already depend on it.

At `Jitter` 0 the feature points sit on the lattice (regular grid); at 1 they move anywhere in
their cell (full Voronoi). So list A and Voronoi are **one solver at two ends of one slider** — but
only after the input is prepared, and that qualification matters:

**`MixtormatCellular` is a square lattice with a 3×3 neighbour scan and no row offset.** It has no
`RowOffset` argument and no non-square `Rows`×`Columns`. Brick, diagonal, stripes and bars are
therefore *not* reachable by calling it with different arguments. The pattern node has to
pre-transform `p` — row shear plus aspect — and hand the helper the transformed point. Everything
it returns, `EdgeDistance` included, then comes back **in that sheared space**, so the bevel and
slope consumers have to be given the inverse or accept anisotropic falloff. Decide which; do not
let a new chat discover it by looking for a `RowOffset` parameter that does not exist.

**`Period` is a single `int`, shared by both axes** (`MixtormatCellPoint`, `MixtormatCellHash`,
via `MixtormatWrapI(Cell.x, Period)`). A non-square lattice — a 4×16 brick wall — needs the wrap
per-axis or the hash disagrees across the seam on the long axis and the pattern tears there. This
is the largest unknown in the plan and belongs in §3's open list.

The good news is that the blast radius is small. Whole-plugin call counts:

| Symbol | Callers |
|---|---|
| `MixtormatCellular()` | 1 — `MixtormatCraquelure.usf:72` |
| `MixtormatCellHash()` | 3 — all in `MixtormatCraquelureGrow.usf` |
| `MixtormatCellPoint()` | internal to the header |
| `MixtormatCellRandom01()` | several, but takes no `Period` — unaffected |

So an `int2 Period` overload that keeps the existing `int` entry point forwarding to
`int2(P, P)` changes nothing for craquelure and unblocks the non-square case. Prefer that to
editing the existing signatures.

Caveat on the ID: `CellId` is a *hash*, not a pixel index — it cannot be the emitted ID directly.
Derive the centre pixel from the feature point: `CentrePixel = (int2)(FeaturePointUV * OutputSize)`.

---

## 3. Recommendation

Build **list A + Voronoi first.** One lattice dispatch plus a call into a helper that already
exists. That covers brick, tile, stripe, bar, diagonal, jittered and organic stone, with correct
edge distance on all of them, so bevel / wear / slope work from day one. Hex second. Herringbone
and basketweave after. Recursive split last, if at all.

Cost note: one dispatch, against Cluster IDs' seventeen and its four full-resolution buffers.
Jittered cuts need a 3-cell neighbour check; Voronoi needs the 3×3 the helper already does.

**Registration order is load-bearing.** `AddPatternIdPasses` must return the R32_UINT map and be
emplaced into `RegionIdMaps` keyed by `SourceChildIndex`, from inside the same producer loop that
already handles Cluster IDs. `FindRegionIdsAbove` takes the *last* entry whose `Key < ChildIndex`
and relies on the array being built in child order — which is true only because that loop iterates
`Layer.Children` in order. A Pattern filter registered from a second loop, or from anywhere after
that one, would silently win over a Cluster filter that sits below it in the stack. Do not reorder
or split that loop.

### Open scope from the user

> "the patterns will have ids, uvs and slope, as well as bevel random and slope blur or worn edges
> features. variation of these is very important. sometimes may there be no slope atall neither
> beveling, sometimes will"

So the node owns **IDs + UVs + slope + bevel randomisation + slope blur / worn edges**, and all of
the relief half is optional. Four things to settle before writing it:

0. **Per-axis `Period`** — see the cellular note in §2. A non-square lattice needs it; the fix is a
   cheap `int2` overload, but it touches a header craquelure depends on, so it goes first.

1. The UV is exact and free for an analytic lattice (`frac(p * Columns)`) — no bbox pass, no
   scatter buffer, none of the 16 bytes/pixel `MixtormatRampIds.usf` spends. Emit it directly.
   Then **transform it per region** — rotation, scale, offset, flip, all salted off the region ID.
   That is the "offset marble and other things" ask, and it is the payoff for the whole node. See
   §5; it lands at one function, `LayerSourceUV` in `MixtormatComposite.usf:304`.
2. Bevels need **roughness and AO**, not just height and normal — and no relief pass in the plugin
   writes either today. See §4; it is a family-wide gap, not a Pattern feature.
3. Pattern's slope overlaps the relief half of Ramp From IDs (Height / Normal / Profile / Feather
   plus the Sobel-and-reorient tail in `MixtormatRampIdRelief.usf`). Decide: **share that pass**
   (Pattern emits the same `float2(Gray, FeatherWeight)` field into `PF_G16R16F` and reuses
   `FMixtormatRampIdReliefCS` via the `FPendingRampTilt` deferral), or duplicate it. Sharing is
   strongly preferred — the reorient tail is already duplicated across erosion, chipping,
   craquelure and ramp, and a fifth copy is a fifth place to drift.

---

## 4. Roughness and AO on the bevels — a gap in the whole family

Not a Pattern IDs feature. A hole in every relief pass already shipped, which Pattern IDs would
otherwise inherit.

**Nothing in the plugin writes roughness or AO from relief.** Verified across all three:

| Pass | Writes |
|---|---|
| `MixtormatCraquelureRelief.usf` | `OutputHeight`, `OutputNormal` |
| `MixtormatRampIdRelief.usf` | `OutputHeight`, `OutputNormal` |
| `MixtormatChipping.usf` | `OutputHeight`, `OutputNormal` (+ state, chips) |

The packed map has the channels sitting unused: `_RAMH` is **R = Roughness, G = AO, B = Metallic,
A = Blend Height** (`Docs/TextureSourceLayout.md:49`), and the composite writes
`OutputRAM[xy] = float4(Roughness, AO, Metallic, F0)` at `MixtormatComposite.usf:675`.

### Why a bevel needs both

A chamfer is a **different surface**, not the same surface bent. On a glazed tile the face is
polished and the cut edge is raw; on stone worn by traffic the edge is polished *smoother* than
the face. Either way the roughness differs, and at grazing angles that difference does more visual
work than the normal does — a chamfer with the face's roughness reads as a decal painted on flat
tile, which is exactly the failure mode "worn edges" is meant to fix.

AO is the other half. A grout line or cell boundary is a concave crease, and the normal map says
"it turns down here" while nothing darkens the contact. Engine SSAO will not catch it: the feature
is below the screen-space radius at any sane texel density. That darkening has to be baked, and
the G channel is what it is for.

### The field is already there

Both consumers want the same input — `EdgeDistance` from `MixtormatCellular.ush`, or the analytic
lattice distance. Roughly:

```hlsl
// Chamfer: the geometric bevel width.
const float Bevel  = 1.0f - saturate(EdgeDistance / BevelWidth);
// Crevice: wider and softer than the chamfer -- the darkening spills past the geometry.
const float Crease = 1.0f - saturate(EdgeDistance / (BevelWidth * AOSpread));

Roughness = lerp(Roughness, EdgeRoughness, Bevel * BevelRoughAmount);
AO        = AO * (1.0f - Crease * AOAmount);
```

Both amounts want the same per-region salted draw the rest of the node uses
(`MixtormatRegionRandomSalted`, new salts alongside `MIXTORMAT_SALT_RAMP`), so bevel width and edge
roughness vary per cell — which is the "variation is very important" requirement applied to the
half of the look that currently has none.

Metallic (B) and F0 are deliberately excluded: a chamfer on a metal tile is still metal.

### The one real decision

Where the RAM write happens.

The deferred relief passes run **after** the composite, in the same scope that already holds
`OutputRAM[WriteIndex]` (see `MixtormatGpuCompositor.cpp:5205` — `AddCopyTexturePass(GraphBuilder,
ShadeRAM, OutputRAM[WriteIndex])` sits a few lines above the ramp tilt loop). So adding a third
target to `FPendingRampTilt` and a third `AddCopyTexturePass` is mechanically cheap.

But post-composite means the write **bypasses the composite's own roughness and AO blending** —
`RoughnessInfluence`, `AOInfluence`, layer alpha and coverage, `MixtormatComposite.usf:569`. That
is arguably correct (the chamfer is this layer's own geometry, and it should not be faded by an
influence slider meant for the base material), but it is a decision, not an accident, and it needs
stating in the tooltip. The alternative — emitting a roughness/AO delta the composite consumes —
is more faithful and much more plumbing.

### Retrofit

Same treatment belongs on craquelure (a crack interior is darker and rougher than the surface it
splits) and chipping (an exposed chip is raw substrate, and its rim occludes). So build it as a
shared helper — `MixtormatEdgeShading.ush` next to `MixtormatColorOps.ush` — not as a Pattern-only
feature. That is the fifth copy of the reorient tail all over again, and this is the moment to not
make it.

---

## 5. Per-region UV transform — varying tiles on one layer

This is the feature that makes the whole thing worth building, and it was missing from the first
draft. §3 said "the UV is exact and free" and stopped there — but a UV nobody transforms is just a
gradient. The point is that **each cell gets its own 2D transform**, so one marble slab shows a
different crop, angle and mirroring in every brick.

Per region, drawn from `MixtormatRegionRandomSalted` off the region ID:

| Control | Range | Note |
|---|---|---|
| Rotation | 0–360°, or 90° steps | see the constraint note below |
| Scale | min/max, uniform or per-axis | affects apparent grain size per tile |
| Offset | random in the source domain | the "different crop of the slab" control |
| Flip U / Flip V | on/off per cell | cheapest, strongest variation |

Composed about the tile centre, not the origin:

```hlsl
float2 P = SourceUV - RegionCentre;
P = mul(Rot(Angle), P * Scale);
SourceUV = P + RegionCentre + RegionOffset;
```

### The constraint that does *not* apply here

`MixtormatUV.ush` opens by saying every transform must map the unit square onto itself — which is
why layer rotation is in 90° steps and layer scale is an integer. That constraint is about the
**layer's** tiling wrap: the compositor `frac()`s each source read, so a transform that breaks
[0,1) seams at the repeat.

**It does not bind a per-region transform.** A cell boundary is already a hard discontinuity —
neighbouring tiles are *supposed* to disagree — so an arbitrary angle and a fractional scale are
both fine inside one cell. This is worth stating plainly, because anyone reading `MixtormatUV.ush`
first will assume the 90°/integer restriction carries over and ship a needlessly crippled control.

Offer both anyway: **orthogonal-only** (4 rotations × 4 flip combinations = 16 orientations, no
resampling artefacts, and the right default for anything with a visible weave or grain) and
**free** (any angle, for stone and marble). One checkbox, not two nodes.

### Where it goes

`MixtormatComposite.usf:304`, `LayerSourceUV(float2 UV)`. Single chokepoint — every layer texture
read goes through it, both from `SampleIncomingHeight` and from the main body's `LayerUV`. So the
insertion is one function, and `RegionIds` is already bound to the composite for the HSV filter.

Structurally the mirror of `ApplyRegionVariation` (`:188`): HSV rewrites colour **after** sampling,
this rewrites the UV **before**. Same map, same salted-draw machinery, opposite side of the sample.

### The asymmetry that matters

A transform about the tile centre needs the region's **centre and extent**, not just its ID.

- **Pattern IDs**: free. The centre is recoverable from the ID (`Id % OutputSize.x`, the same trick
  Ramp uses) and the extent is `float2(1.0/Columns, 1.0/Rows)` — a shader constant.
- **Cluster IDs**: not available without the bounding-box pass that `MixtormatRampIds.usf` already
  implements (stages INIT/BOUNDS/RESOLVE, `RWStructuredBuffer<int> RegionBounds` strided
  `Index*4 + k`, with `WrapDelta` for regions straddling the seam).

So per-region UV should ship **Pattern-only first**. Extending it to Cluster IDs means factoring
that bbox pass out of Ramp into something both can call — worth doing, but it is a second piece of
work and should not gate the first.

### Two things to check when it runs

- **Everything samples `SampleLevel(..., 0.0f)`.** There are no mips anywhere in the composite, so
  there is no derivative discontinuity to fix at cell boundaries — the usual per-tile-UV problem
  does not exist here. The flip side is no minification filtering either: per-tile *scale* is the
  first control that will make that visible, on tiles scaled down. Pre-existing, not a regression,
  but this is where it will first be noticed.
- **`MIXTORMAT_INVALID_REGION`** must fall through to the untransformed `LayerSourceUV`, not to a
  transform built from a garbage centre.

---

## 6. File map — every touchpoint

Traced from `RampId`, which is the closest precedent (a Filter-family child that computes
pre-composite and defers a post-composite height/normal pass). Line numbers are as of this writing;
treat them as anchors, not addresses.

### Runtime — `Source/MixtormatRuntime/Public/MixtormatMaterial.h`

| What | Where | Note |
|---|---|---|
| `FMixtormatPatternFilter` USTRUCT | near `:1339` (`FMixtormatRampIdFilter`) | |
| `EMixtormatLayerChildType::PatternId` | `:1463` | **Append only.** `Type` is serialised by value — inserting renumbers every saved asset. |
| `FMixtormatPatternFilter PatternId;` on `FMixtormatLayerChild` | `:1499` | with `meta = (EditCondition = "Type == EMixtormatLayerChildType::PatternId")` |

**This file is the one that gets forgotten.** A previous pass wrote the shader, the compositor and
the whole editor wiring and never touched it, so nothing compiled at all. UHT is also the only
thing that validates a new `USTRUCT`, which is why the build has to run even when the link fails.

### Shaders — `Shaders/Private/`

| File | Status |
|---|---|
| `MixtormatPatternIds.usf` | new — the lattice/Voronoi solve, emits R32_UINT IDs (+ UV, + ramp field) |
| `MixtormatCellular.ush` | existing — include it, do not reimplement |
| `MixtormatRegionId.ush` | existing — `MIXTORMAT_INVALID_REGION`, `MixtormatRegionSeed`, salts |
| `MixtormatDebugColor.ush` | existing — use `MixtormatDebugRamp` / `MixtormatDebugColor`, never raw constants |
| `MixtormatRampIdRelief.usf` | existing — reuse if slope is shared |

Two rules that have cost time before:

- **Includes are full virtual paths.** `#include "/Plugin/MaterialLab/Private/MixtormatUV.ush"`.
  A relative include resolves fine under standalone `dxc` (filesystem sibling) and fails in UE, so
  shader validation cannot catch it.
- **Every file-scope uniform must be declared in the shader parameter struct**, whether the entry
  point or the current `Stage` reads it or not. UE binds the whole file, not the entry point's
  subset. `MixtormatRampIds.usf:10` says this in its own header comment.

### Compositor — `Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp`

| What | Anchor |
|---|---|
| `FMixtormatPatternIdsCS` + `IMPLEMENT_GLOBAL_SHADER` | `:1294` (`FMixtormatRampIdsCS`) |
| `FPatternIdRenderData` struct | `:1644` (`FRampIdRenderData`) |
| member on `FChildRenderData` | `:1671` |
| gather from `FMixtormatLayerChild` | `:2222` — set `Type`, `SourceChildIndex`, copy fields, sanitise with `FMath::IsFinite` |
| `AddPatternIdPasses(...) -> FRDGTextureRef` | `:1943` (`AddRampIdPasses`) |
| register into `RegionIdMaps` | `:3231` |
| producer loop `if (Child.Type != Filter) continue;` | `:3188` — **must also accept `PatternId`** |
| consumer-demand scan | `:3207`–`:3221` — the `break` on a later producer must also break on `PatternId`, and the `bWanted` list already names Hsv/Random/Ramp |

Two traps in that block:

- **Demand culling.** A producer with nothing reading it and no preview selected must not run.
  Cluster IDs is seventeen dispatches; Pattern is one, so the stakes are lower, but the loop is
  shared and skipping the cull would be inconsistent.
- **Debug writes are gated separately from whether the pass runs.** `bIsSelectedPreview` is a
  distinct flag from `bWanted`, passed into the pass as a `WriteDebug` uniform. Writing the debug
  target unconditionally was a real bug: a producer on a later layer overwrote whatever preview an
  earlier layer's composite had published, so the mask-height eye toggle showed cluster IDs.

### Editor

| File | What |
|---|---|
| `Source/MixtormatEditor/Private/Widgets/SMixtormat.h:160` | `AddPatternIdToLayer`, `GetSelectedPatternId` ×2 (const + non-const), `BuildPatternIdControls` |
| `SMixtormat_Layers.cpp:275` | mask-selection guard |
| `SMixtormat_Layers.cpp:395` | selected-maps caption |
| `SMixtormat_Layers.cpp:1136` | child display name |
| `SMixtormat_Layers.cpp:1171`, `:1304`, `:1664`, `:2362` | the child-row type lists (row styling, eye affordance, remove affordance) |
| `SMixtormat_Layers.cpp:1396` / `:2424` | enable getter / setter |
| `SMixtormat_Layers.cpp:1559` | add-child context menu item |
| `SMixtormat_Layers.cpp:1706` | remove label |
| `SMixtormat_Layers.cpp:2206` / `:2223` / `:2234` | `AddPatternIdToLayer`, `GetSelectedPatternId` ×2 |
| `SMixtormat.cpp:564` | the disable-others block |
| `UI/Layers/MixtormatLayerBadges.cpp:174` / `:198` | which types carry a badge, and its label |
| `SMixtormat_Inspector.cpp:1477` | `BuildPatternIdControls()` |
| `SMixtormat_Inspector.cpp:3415` | the scroll-box slot |
| `SMixtormat_Inspector.cpp:3399` **and** `:3439` | **both** visibility lambdas |

**The `3399`/`3439` pair is the one that bites.** `3399` is "show the child inspector"; `3439` is
its inverse, "show the layer inspector". The layer one defaults to Visible, so a type missing from
the *second* list renders both panels stacked. Both lists must name every child type. There is a
comment there saying so.

**UI rules:** no hardcoded sizes or colours — everything comes from `MixtormatTokens::*`. Use the
existing atoms: `MixtormatRow::Make/MakePair/MakeChip/MakeCheckbox`, `MakeMemberSlider<T>`,
`MakeMemberSliderInt<T>`, `AddMaskShapingRows()`, `SMixtormatSegmentedControl`,
`SMixtormatTabStrip`, `SMixtormatBadge`. Do not re-sprawl parameters by hand.

**One crash to avoid, already hit once:** never pass a reference into an array you are about to
grow — `Palette.Add(Palette.Last())` reallocates out from under the argument and asserts. Copy to
a local first.

---

## 7. Build

```bash
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" MatLabEditor Win64 Development -Project="C:\Tools\MaterialLab\MatLab\MatLab.uproject" -NoHotReloadFromIDE -WaitMutex -FromMsBuild
```

`LNK1104` at the end is **expected and fine** while the editor is open. Compile + UHT are what
matter, and UHT is the only thing that validates a new `USTRUCT`.

Hugo verifies in the editor. Do not run test sweeps or `BuildEditor.bat`.

---

## 8. Related documents

- `Prototypes/BetterClustering/final_imp/FILTERS_DESIGN.md` — the Filter family design and the
  "suggested order" this work follows.
- `Prototypes/BetterClustering/CLUSTER_ID_DESIGN.md` — the segmentation itself.
- `Docs/ErosionEffectPlan.md` — where the periodic-lattice lesson is recorded.
- `Docs/ChippingEffectPlan.md`, `Docs/CraquelureReview.md` — the other consumers of
  `MixtormatCellular.ush`.
