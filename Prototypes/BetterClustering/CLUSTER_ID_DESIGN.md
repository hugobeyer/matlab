# Cluster ID map — what it is and what it's for

Design note. Nothing here is implemented in Mixtormat yet; this describes the goal, the mechanism
proven in the Houdini graph in this folder, and the things the output unlocks.

---

## The goal in one line

Turn a surface's own height map into a **per-region ID map**, so that later passes can vary each
region independently — colour, rotation, gradients — without any of it being authored by hand.

The immediate use is subtle HSV variation: the same brick texture where every brick is a slightly
different tone, every pebble a slightly different value, and none of it painted. The ID map is the
thing that makes "every X" addressable.

---

## Why an ID map, and not noise

Overlaying noise on a texture varies the *pixels*. It reads as dirt on the lens, because it
ignores what the surface is made of — a noise blob straddles three bricks and half a mortar line.

Varying by region reads as *material*, because the variation lands on the same boundaries the eye
already sees. That requires knowing which pixels belong together, which is what the ID map is.

The key property: **regions come from the texture itself**, not from a grid laid over it. Sizes
vary organically because the surface's features vary organically. There is no cell size to pick
and no uniformity to hide.

---

## The pipeline

```
TX_Concrete_Cracked_01_RAMH.png
        │
   channel split
        │
        ├── alpha (height) ──► EQUALIZE ─────────────┐
        │                      stretch to −1 … 1     │  (as filter input)
        │                                            ▼
        └── red ─────────────► FILTER FREQUENCIES ──► SEGMENT BY CONNECTIVITY ──► ID map
                               directional FFT wedge   threshold / offset / collapse
```

Input is the packed RAMH map every Mixtormat surface already carries, so this needs no new
authoring and no new assets — the height it segments on is the height already in the alpha
channel, including the case where it was derived from the normal.

Output is an integer ID per pixel. Rendered with a random colour per ID it looks like
`result.png`: thousands of irregular organic regions following the concrete's grain and cracks,
sizes ranging from a few pixels to large patches, no lattice visible anywhere.

---

## Stage 1 — Equalize

**What it does:** renormalises the height into a fixed, symmetric −1 … 1 range.
Mode "Stretch to Black and White", Black = −1, White = 1, Luminance Type = Value.

**Why it matters, and why it is not optional:** the segmentation's `Threshold` is an absolute
number. Without equalisation it means something different on every texture — one scan's height
occupies 0.2–0.6, another's the full 0–1, and the same threshold produces wildly different
granularity on each. Equalising first makes **one threshold value mean the same thing on every
input**, which is what makes the control shippable rather than per-asset guesswork.

Symmetric about zero rather than 0 … 1 so that `Offset` slides band boundaries evenly in both
directions.

**How it works** (`equalize_*.cl`): a statistics pass gathers count / sum / min / max, a reduction
folds the partials together, then `compute_gain_and_shift` derives a single gain and shift from
the requested mode:

| Mode | Derives |
|---|---|
| Stretch | `gain = (white−black)/(max−min)`, `shift = −min·gain + black` — the mode used here |
| Min | pins the darkest value to black |
| Max | pins the brightest to white |
| Avg | drives the mean to a target |
| Symmetric | scales by whichever of \|min\|, \|max\| is larger |

The gain and shift then feed an ordinary brightness node. Statistics on one side, application on
the other — the same split Mixtormat's compositor already uses everywhere.

---

## Stage 2 — Filter Frequencies (optional, and the real art direction)

**What it does:** FFT → multiply by an angular wedge mask → inverse FFT. Isolates features by
*direction and scale* before anything is segmented.

`filterfrequencies.cl` builds the mask: for each frequency-domain pixel it takes the normalised
direction from the image centre, rotates by `angle`, and keeps a wedge of angular half-width
`width`, with `mirror` to take both opposing lobes and `smooth` to feather rather than hard-cut.

**Why it's here:** this is where the decision of *what counts as a region* actually gets made.
Segmentation itself has almost no parameters; the interesting control moved upstream. Filtering to
low frequencies gives large regions following broad structure; keeping high frequencies gives fine
grain. Restricting the angle picks out directional features — plank runs, brushed metal, wood
grain — and suppresses everything crossing them.

Treat this as the creative dial and the threshold as a granularity dial.

---

## Stage 3 — Segment by Connectivity

The core. GPU union-find (connected components) over the filtered signal.

### The three kernels

**`segconnectivity_assignidx.cl`** — every pixel starts as its own class, labelled with its linear
index `@ix + @iy·@xres`. Pixels failing the cutoff are set to `−1`, meaning "not part of any
region"; those become the alpha=0 areas of the output.

**`segconn_findclass.cl`** — the union-find itself, run iteratively.

- `_findClass` walks the parent chain to the root, then path-compresses what it walked with
  `atomic_min`. The comment is explicit that this is safe under contention because it only ever
  shortens paths through an existing tree, never re-roots.
- `_disjointUnion` finds both roots and merges them, always keeping the smaller index as the new
  root, committing with `atomic_cmpxchg` and retrying if another thread re-rooted underneath.
- Each pixel only tests its **left and up** neighbours. That's sufficient — every adjacency gets
  examined once from one side, so the full 4-connectivity is covered without doing it twice.
- Wrapping is honoured via `@dst.border == IMX_WRAP`, so **the ID map tiles** when the source
  does. That matters for Mixtormat, where everything wraps.

Two comparison modes decide whether neighbours may merge:

| Mode | Test | Result |
|---|---|---|
| `@above == 2` | `signbit(a − threshold) == signbit(b − threshold)` | binary: above and below split into two region sets |
| `@above == 3` | `_computelevel(a) == _computelevel(b)` | **levels: the mode that yields a full ID map** |

`_computelevel` is simply `floor((value − offset) / threshold)` — a band index. Two neighbours
join only if they fall in the same band.

A `@WRITEBACK` pass resolves every pixel to its final root and sets `alpha` to 0 or 1 depending on
whether the pixel belongs to any region at all.

**`segconn_writeback_unique_ids.cl`** — after union-find, the surviving IDs are the sparse set of
root indices: arbitrary pixel indices scattered across the whole range. This remaps them through a
lookup produced by a Layer-to-Points round trip (`unique_ids` → `representative_ids`) into a
**dense 0…N−1 sequence**. That's the "Collapse IDs" toggle.

---

## The parameters

Three, and only three.

| Parameter | What it controls |
|---|---|
| **Threshold** | Band width, and therefore region granularity. Over a −1 … 1 range, 0.1 gives 20 bands. Larger → fewer, bigger regions. This is the scale dial |
| **Offset** | Where band boundaries fall. Same granularity, different partition — a reseed that doesn't change the character |
| **Collapse IDs** | Remap sparse root indices to a dense 0…N−1 range. Wanted whenever anything downstream hashes the ID or indexes an array by it |

Notably absent, and deliberately: no cell size, no compactness, no iteration count, no cluster
count, no random-size-range, no feature weights. Region shape and size come from the image.

### Micro and macro from the same node

Two scales is **a second pass at a larger threshold**. Wider bands, larger regions, same node, no
second algorithm. Worth confirming in practice whether the two levels nest cleanly enough to sum
their variation without boundary disagreement — bands are not strictly hierarchical, so a coarse
region can in principle straddle a fine boundary.

---

## What the ID map is for

### 1. HSV variation — the reason this exists

Hash the ID into a small signed offset and apply it to hue, saturation and value. Every region
gets its own consistent tint; boundaries land exactly on the features the eye already reads.

This lands directly on machinery Mixtormat already has: `AdjustHSV` in `MixtormatComposite.usf`
performs exactly those three operations, in that order, and already carries `HueShift`,
`Saturation` and `Value`. Adding a per-ID offset to them is the entire integration. The layer-level
controls stay as they are; the variation rides on top.

Amounts stay small — a hue shift of a couple of percent is already clearly visible on a flat
surface. The goal is "these bricks came from the same kiln but not the same firing", not a
rainbow.

### 2. Per-ID UVs from the region centroid

With a centroid per ID, every region gets a **local coordinate frame**: position relative to its
own centre, rather than the global UV. That unlocks:

- **Random rotation per region** — hash the ID into an angle and rotate the local frame. Detail
  laid down through it stops repeating identically across regions
- **Ramped colour per region** — a gradient running across each region individually, oriented by
  that random angle. This is what Substance's *Flood Fill to Gradient* does, and it is what makes
  tiles read as individually tilted rather than uniformly lit
- **Per-region texture placement** — a decal or detail map placed once per region rather than tiled
  over the whole surface

The centroid also gives radial distance, so a region can be shaded from its centre outward —
darkening at edges, or a worn centre.

### 3. Statistics by ID

The Houdini graph already computes count / sum / min / max per region via Prefix Sum. Those turn
into variation that **follows the material** rather than being pure noise:

| Statistic | What it can drive |
|---|---|
| Mean height | tint by how proud or recessed a region sits — high pieces catch light, low ones stay dark |
| Max − min | tint by how busy a region is — smooth faces vs broken ones |
| Area (count) | large patches read differently from chips and grains |
| Centroid + second moment | elongation and orientation, so long thin regions can be treated differently from blobs |

The distinction worth keeping: a hash knows nothing about the surface, a statistic does. A hash
gives variety; statistics give variety that looks like it was caused by something.

### 4. Region selection as a mask

An ID map is also a *selector*. Pick a subset of IDs — by hash, by statistic, by area — and you
have a mask of "some of the bricks", "the recessed pieces", "the largest fragments". That feeds
any existing Mixtormat effect: put stain only in the low regions, wear only on the proud ones,
chipping only on the large fragments.

This is the same shape as the existing `FMixtormatColorIdMask`, which already selects regions of
an ID map by colour with tolerance and softness — so there is a consuming node in the codebase
already.

### 5. Boundary and adjacency derivatives

The ID map's discontinuities are the region boundaries, for free. Distance-to-boundary gives a
per-region bevel or edge wear. Neighbour IDs give adjacency, which is what a *proper* macro
grouping would flood over — grouping regions that actually touch, rather than ones that merely sit
near each other.

---

## Why this over a grid-based approach

Recorded because the alternative was attempted first, in `../Clustering`, and abandoned.

SLIC superpixels impose a regular grid and pull it toward image edges via a compactness term. That
brings a parameter set — grid spacing, compactness, feature weights, iteration count, orphan
merging — all of which need tuning per texture, and it still produces roughly uniform regions,
which is exactly the look to avoid.

Union-find on a quantised signal has none of that. It is exact rather than iterative, it needs no
size parameter because size comes from the image, and it produces the organic size distribution
for free. `../Clustering` should be treated as superseded.

---

## Open questions for the port

- **Where the ID map lives.** `FMixtormatColorIdMask::IdTexture` is a `TSoftObjectPtr<UTexture2D>`
  — an asset. A live cluster pass produces a transient RDG texture and the two don't connect.
  Either give that node a source switch, or bake through `MixtormatBakeService` (which already
  writes `SRGB=false` / `TC_Masks`).
- **Node placement.** Sibling to `Craquelure` as a layer child type, not an effect — it needs the
  surface maps as input and belongs in the mask chain, before the layer composites.
- **Union-find in RDG.** The kernels use `atomic_min` and `atomic_cmpxchg` on a raw integer buffer.
  Mixtormat's compositor currently binds only textures — there is no
  `SHADER_PARAMETER_RDG_BUFFER` anywhere in it. This is the one piece of genuinely new plumbing.
- **Caching.** Segmentation is expensive and its inputs change rarely, while the variation amounts
  change constantly. Hash the segmentation parameters into a key like `MixtormatNetworkKey` and
  keep the HSV amounts out of it, or every hue nudge re-segments.
- **Iteration count.** Union-find is run repeatedly until stable. How many passes are needed at
  4K, and whether that count needs to scale with resolution the way craquelure's does.
