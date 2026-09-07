# Cluster ID prototype — Houdini COPs

SLIC superpixels over a surface's own maps, grouped into uneven macro clusters, driving subtle
per-cluster HSV variation. Prototyped here so the algorithm can be judged by eye before any of
the Unreal RDG plumbing exists.

Reference: [gSLICr — SLIC superpixels at over 250Hz](https://arxiv.org/abs/1509.04232)
(Ren, Prisacariu, Reid). Original method: Achanta et al., PAMI 2012.

---

## The layout decision, and why it matters for the port

Every pass runs over one of two layers:

| Layer | Resolution | Holds |
|---|---|---|
| the image | full | `label` — which cluster each pixel joined |
| **centres** | **the cluster grid** | one texel per cluster: position, feature, pixel count |

So "cluster *(i,j)*" is literally texel *(i,j)* of the centres layer. That single choice removes
the two things that would otherwise make this hard: no atomics, and no structured buffers. A
per-cluster reduction becomes "one texel scans its own 2S×2S window", which is `#runover layer`
like everything else.

It also decides the Unreal port. `MixtormatGpuCompositor` binds **only textures** today — there
isn't a single `SHADER_PARAMETER_RDG_BUFFER` in it. A centres *texture* ports as a normal RDG
target; a cluster *buffer* would mean new plumbing of exactly the kind that produced the
`ResolveCS` binding error dxc couldn't catch.

---

## Wiring

Six kernels, one OpenCL COP each.

```
                    ┌─ 01 seed ──────────────┐
  height ───────────┤                        ├──► centre_pos, centre_feat
  (source maps)     └────────────────────────┘
                                │
                    ┌───────────▼────────────┐
                    │  02 assign  (→ label)  │   ◄── loop these two,
                    │  03 update  (→ centres)│       5–10 rounds
                    └───────────┬────────────┘
                                │
              ┌─────────────────┼──────────────────┐
              ▼                 ▼                  ▼
        04 macro_merge    05 id_to_colour    06 hsv_variation
        (centres→macro)     (debug view)      (the payoff)
```

**Iterating 02/03** — a `for-loop` COP over the pair, or just copy the two nodes 5–10 times in a
row while prototyping. SLIC converges fast; the last rounds move almost nothing.

**Getting `macro` back to pixel resolution for 06** — `04` writes a macro index per *micro
cluster*, so it lives on the centres layer. Look it up per pixel through `label`, or add a tiny
scatter pass. Simplest while prototyping: bind the centres layer into 06 and index it with
`label`.

**Bindings** — real `#bind` directives sit at the top of each kernel, in the form Houdini's own
`VEXpressions.txt` and the OpenCL COP creation script use:

```
#runover layer
#bind layer src                       // read
#bind layer mask? float val=1         // optional, typed, with a default
#bind layer dst noread write          // output
#bind parm bright float val=1         // parameter
```

Paste a kernel in, then press **"Create inputs and spare parameters"** — Houdini reads the
directives and generates the matching inputs and parms.

Two things to watch:

- **Run-over target is the first writable layer.** That's deliberate per kernel: `02`, `05` and
  `06` run at image resolution, while `01`, `03` and `04` run over the *centres* layer, so their
  writable binding is listed first. Set the centres layer's resolution to the cluster grid.
- **`@ixy` and `@res` may need enabling** on the node's Bindings tab. Unlike VEX, OpenCL doesn't
  bind globals just because you referenced them.

### Verified against Houdini 22's own docs

Found the hard way; noting so it isn't rediscovered.

- **`#bind parm` takes integer, float and *float* vectors only.** There is no `int2` parameter
  type — it errors with *"Illegal type for parameter 'int2'"*. Resolutions and grid dims are
  therefore bound as pairs of plain `int` parms (`grid_x` / `grid_y`).
- **Name decorations**: `&` = write, `?` = optional, `!` = noread. So `!&dst` is a write-only
  output, and `src?` is an optional input.
- **Always give a scalar layer an explicit `float` flag.** With no type flag a layer binds at its
  *actual* channel count, so an RGBA input hands back a `float4` and you get
  *"initializing 'float' with an expression of incompatible type 'float4'"*. Every single-channel
  binding here says `float`; every three-channel one says `float3`.
- **Randomness** comes from `#import <random.h>` — `SYSwang_inthash` to decorrelate an id, then
  `SYSfastRandom(&seed)` to draw from it. Houdini's own pair, as used by the shipped kernels.
- **`float2` parms and layers are legal** — it's specifically *int* vectors that aren't.
- **Accessors** used here: `@layer` alone is a bilinear sample at the current output position;
  `@layer.bufferIndex(ixy)` reads an exact texel — which is how a full-res pass reads the tiny
  centres layer, and how a centres-res pass reads back into the image.
- **`.set(v)` requires the layer be aligned to the output buffer.** That's why each kernel lists
  its own run-over target first. `@layer.setIndex(ixy, v)` exists for writes that aren't aligned.

---

## Parameters

**02 assign — the ones that decide the look**

| Parm | What it does |
|---|---|
| `grid` | cluster grid dims. This *is* the micro scale — in SLIC `S = sqrt(pixels/K)`, so grid and cluster count are one control, not two |
| `compactness` (m) | high = uniform blobs, low = clusters hug the texture's edges. **Low is what you want** — a cluster straddling a mortar line tints half a brick |
| `w_height` / `w_normal` / `w_albedo` | what "similar" means. Normal-heavy → facets. Height-heavy → tiles. Albedo-heavy → material zones. **Not in the paper** — Achanta clusters photos in CIELAB; you have real surface maps, so this is the most useful dial you have |
| `tiling` | wrap on the UV period. Photos don't tile, your textures do — off, and clusters mismatch across the seam, which is precisely where a hue shift shows |

**04 macro_merge** — a weighted Voronoi over the cluster *centroids*

| Parm | What it does |
|---|---|
| `macro_grid_x/y` | how many macro seeds. Coarser than the micro grid — 6×6 over a 32×32 micro grid means ~28 micro cells per macro group on average |
| `size_variation` | **the uneven-size dial.** Each seed gets a random weight and the distance is divided by it, so a heavy seed reaches further and swallows more cells. 0 = even macro cells, 1 = some big patches and some tiny ones |
| `jitter` | scatters the macro seeds off their lattice so the macro layer doesn't read as a grid of its own |
| `seed` | deterministic. The grouping must survive a re-cook or preview won't match render |

**06 hsv_variation** — `hue_var`, `sat_var`, `val_var`, `macro_mix`.
Keep them small: `hue_var = 0.02` is already clearly visible on a flat surface.

---

## Statistics by ID

`03` doesn't only move the centroids — it's the per-cluster reduction, so it writes a statistics
record while it's there. Both output layers carry it:

| Channel | Holds |
|---|---|
| `centre_pos.xy` | centroid, in source pixels |
| `centre_pos.z` | **pixel count** — the cluster's area |
| `centre_feat.x` | mean height |
| `centre_feat.y` / `.z` | **min / max height** — so `max - min` is how busy the cluster is |

The min/max were free: those two channels were already sitting unused.

`06` can then blend hash-driven variation against **stat-driven** variation via `stat_mix`:

- at `0` — pure noise, every cluster independent
- turned up — value follows how high the cluster sits, saturation follows how busy it is

That's the difference between variation that reads as *material* and variation that reads as dirt
on the lens. A hash says nothing about the surface; the mean and the range do.

Obvious extensions once this is wired, all free from the same pass: elongation (from a second
moment), edge-adjacency count, mean normal for facet grouping.

### The reduction pattern to copy for the port

The prototype's `03` has each centre scan its own 2S×2S window. That's fine here and needs no
atomics, but it does redundant reads and misses any pixel that strayed outside the window.

SideFX's own two-pass shape is better and is what the Unreal version should use:

1. one pass accumulates **per-block partials** into distinct indices — no atomics, because every
   block writes its own slot
2. a second pass runs over a single element (`if (@elemnum) return;`) and folds the partials
   together with `.getAt(i)` / `.len`

That's exact rather than windowed, and it generalises to any statistic.

---

## Look at 05 before wiring 06

The debug view is the point of prototyping here. What you're judging:

- do brick faces come out **whole**, or split down the middle?
- do mortar lines land **on** cluster boundaries?
- is the size in the right neighbourhood?

If they don't, the fix is the **feature weights in 02**, not the variation amounts in 06. Getting
that wrong is the failure mode where you spend a day tuning hue and the real problem was
`w_normal`.

---

## Known gaps, deliberately

- **Feature vector is height-only in the centre record.** `centre_feat` carries height; normal
  and albedo are read from the image at the centre position instead of being stored. Fine for
  judging the look, wrong for a shipped version — pack a proper record when porting.
- **`04` is a weighted Voronoi, not a region-grow over the adjacency graph.** A true graph flood
  would follow the micro clusters' actual connectivity and give shapes that respect the texture's
  structure; the Voronoi only knows centroid positions, so a macro boundary can cut across a
  feature the micro clusters had correctly separated. Good enough to judge the look, and it is
  O(9) per cell with no iteration — but the graph flood is what the Unreal version should do.
- **No connectivity enforcement.** Raw SLIC can leave orphaned fragments, and for HSV jitter an
  orphan is one pixel of a different hue — a sparkle. The paper's post-step merges orphans into
  the nearest label. Add it once the rest looks right.

---

## Porting notes

- `AdjustHSV` in `MixtormatComposite.usf` already does exactly the three operations in `06`, in
  the same order, and already carries `HueShift` / `Saturation` / `Value`. The port is adding a
  hashed per-cluster offset to those, not writing anything new.
- Architecturally the cluster node is a **child type, sibling to `Craquelure`** — not an effect.
  Effects are Surface/Filter class and run *after* the layer composites, which is why craquelure
  relief needed `FPendingCraquelureRelief` to defer. Clustering needs the surface maps as input
  and belongs in the mask chain.
- **Cache it like craquelure.** Hash the clustering parameters into a `MixtormatNetworkKey`-style
  key and keep the HSV variation amounts *out* of it — otherwise every hue nudge re-clusters.
  Same lesson as moving `Warp` out of the craquelure key.
- **Open question:** `FMixtormatColorIdMask::IdTexture` is a `TSoftObjectPtr<UTexture2D>` — an
  asset. A cluster pass produces a transient RDG texture, and those don't connect. Either give
  ColorId a source switch (asset *or* live cluster buffer), or bake the ID map through
  `MixtormatBakeService`, which already writes `SRGB=false` / `TC_Masks` and would work with
  ColorId today unchanged. Worth deciding before the port, not during.
