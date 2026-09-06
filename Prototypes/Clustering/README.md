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

---

## Parameters

**02 assign — the ones that decide the look**

| Parm | What it does |
|---|---|
| `grid` | cluster grid dims. This *is* the micro scale — in SLIC `S = sqrt(pixels/K)`, so grid and cluster count are one control, not two |
| `compactness` (m) | high = uniform blobs, low = clusters hug the texture's edges. **Low is what you want** — a cluster straddling a mortar line tints half a brick |
| `w_height` / `w_normal` / `w_albedo` | what "similar" means. Normal-heavy → facets. Height-heavy → tiles. Albedo-heavy → material zones. **Not in the paper** — Achanta clusters photos in CIELAB; you have real surface maps, so this is the most useful dial you have |
| `tiling` | wrap on the UV period. Photos don't tile, your textures do — off, and clusters mismatch across the seam, which is precisely where a hue shift shows |

**04 macro_merge**

| Parm | What it does |
|---|---|
| `min_cells` / `max_cells` | macro group size range, in micro cells. Uneven sizes are the point — uniform macro regions are the tell that SLIC was involved |
| `seed` | deterministic. The grouping must survive a re-cook or preview won't match render |

**06 hsv_variation** — `hue_var`, `sat_var`, `val_var`, `macro_mix`.
Keep them small: `hue_var = 0.02` is already clearly visible on a flat surface.

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
- **`04` walks a serpentine grid rather than a real adjacency graph.** Runs stay adjacent because
  the walk reverses each row, but a proper flood over the micro adjacency graph gives better
  shapes. That's what the Unreal version should do; this fits in one dependency-free kernel and
  is enough to judge whether uneven macro sizes read right.
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
