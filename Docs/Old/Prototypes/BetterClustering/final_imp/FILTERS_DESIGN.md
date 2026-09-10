# Filters — cluster IDs and per-region variation

Design note for four new features and the category that holds three of them. Nothing here is
implemented. The four kernels in this folder are the proven prototypes.

---

## The goal

Vary a surface **per region** rather than per pixel. Every brick a slightly different tone, every
pebble a slightly different value, every shard of cracked paint drawn from a palette you chose —
none of it authored, all of it derived from the texture's own structure.

Overlaying noise varies pixels, and reads as dirt on the lens because it ignores what the surface
is made of: a noise blob straddles three bricks and half a mortar line. Varying by region reads as
*material*, because the variation lands on the boundaries the eye already sees.

That needs one thing the plugin doesn't have: a way to know **which pixels belong together**.

---

## A new category: Filter

`EMixtormatLayerChildType` currently holds `Mask`, `Effect`, `Generated`, `Craquelure` and
`ColorId`. Three of the four features below fit none of them, so **Filter** joins as a sibling.

### Why not a mask

A mask child emits **0…1 coverage** and blends into the accumulating mask chain through a
`BlendMode` and a `Weight` lerp. That contract is what makes masks composable.

A cluster node emits an **integer ID per pixel**. Ask what `Max` of two ID maps means, or
`lerp(idA, idB, 0.5)`, and there is no answer — the result is a third id that means nothing.
An ID map fails every operation the mask chain requires. It is *data*, not coverage.

The proof this distinction is real: **`FMixtormatColorIdMask` already exists, and it is a mask
that consumes an ID map.** Producing an ID map and selecting from one are already two different
things in this codebase; one of them is already a mask, and the other is what Filter is for.

### Why not an effect

Effects are Surface or Filter class (`MixtormatEffectClassOf`) and run **after the layer
composites** — which is exactly why craquelure relief needed `FPendingCraquelureRelief` to defer
itself. A cluster node needs the surface maps as *input* and must run *before* the composite so
its output is available to the mask chain and to the colour stage. Different position in the
graph, different category.

### The discriminator, stated once

> **Does the output blend into the mask chain as coverage?**
> Yes → Mask.  No, and it runs before the composite → Filter.  No, and it runs after → Effect.

---

## The four features

```
   surface maps (RAMH)
          │
          ▼
   ┌─────────────────┐
   │ 1. CLUSTER IDS  │  Filter        →  ID map (int per pixel)
   └────────┬────────┘
            │
   ┌────────┼──────────────┬──────────────────┬──────────────────┐
   ▼        ▼              ▼                  ▼                  ▼
┌──────────────┐ ┌───────────────┐ ┌────────────────┐ ┌──────────────────┐
│ 2. RANDOM    │ │ 3. HSV        │ │ 4. RAMP / UVS  │ │ ColorId (exists) │
│    VALUE     │ │    FROM IDS   │ │    FROM IDS    │ │                  │
│    Mask      │ │    Filter     │ │    Filter      │ │    Mask          │
│ → 0…1        │ │ → albedo      │ │ → uv + 0…1     │ │ → 0…1 coverage   │
└──────────────┘ └───────────────┘ └───────┬────────┘ └──────────────────┘
                                           │
                            ┌──────────────┴───────────────┐
                            ▼                              ▼
                  per-region material mapping     × height  → per-region tilt
                  (warp / detail placement)
```

---

## 1. Cluster IDs — Filter

**Prototype:** `FINAL_IMPLEMENTATION.cl`

Segments the surface into regions that follow its own structure, and emits an integer ID per
pixel. Sizes vary organically because the surface's features do — there is no grid, no cell size,
and no uniformity to hide.

### How it works

One kernel, staged by `@Iteration`, run ~20 times:

| Iteration | Stage |
|---|---|
| 0 | seed four atomic slots |
| 1 | atomic min/max over height and roughness |
| 2 | normalise both to 0…1 → `guide`, `signal` |
| 3 | `par[idx] = idx` — union-find init |
| 4 – 15 | 12 union passes with pointer-jump path compression |
| 16 | resolve roots → `id`, optional `preview` |

**Equalisation is inlined** rather than a separate node. `f2ord`/`ord2f` map floats to
order-preserving unsigned ints so integer `atomic_min`/`atomic_max` work directly on them — the
whole statistics pass in a single iteration.

That normalisation is **not optional**. `threshold` is an absolute number; without renormalising
first it means something different on every texture, because one scan's height occupies 0.2–0.6
and another's the full range. Normalising makes one threshold value mean the same thing on every
input, which is the difference between a shippable control and per-asset guesswork.

**The merge criterion is two-channel.** Roughness is quantised into bands by
`qlevel(v) = floor((v − offset) / threshold)`, and two neighbours join only if they share a band
**and** `|Δheight| × height_influence ≤ threshold`. A region must be uniform in roughness *and*
not step in height. At `height_influence = 0` it falls back to pure roughness bands.

**Union-find, not iterative clustering.** `hook()` reads both parents, one level of indirection
each, and commits with `atomic_min` on the higher root. Only left and up neighbours are tested —
every adjacency is examined once from one side, which covers full 4-connectivity without doing it
twice. `border=WRAP` on the bindings plus explicit index wrapping means **the ID map tiles**.

### Parameters

| Parameter | Controls |
|---|---|
| **Threshold** | Band width, and therefore region granularity. Internally `clamp(t,0,1) × 0.25` over a 0…1 signal, so the 0…1 dial spans ~4 bands to hundreds. The scale control |
| **Offset** | Where band boundaries fall. Same granularity, different partition — a reseed that doesn't change character |
| **Height Influence** | How strongly a height step blocks a merge. 0 = roughness bands only |
| **Collapse IDs** | *(not in the prototype)* Remap sparse root indices to a dense 0…N−1 range |

Deliberately absent: no cell size, compactness, iteration count, cluster count, size range or
feature weights. Region shape and size come from the image.

### Known limits of the prototype

- **`hook()` doesn't retry.** The multi-node Houdini version loops on `atomic_cmpxchg` until it
  wins; this fires one `atomic_min` and lets a lost race resolve on a later pass. Fine across 12
  passes, but approximate per-pass rather than exact.
- **The root walk caps at 16 steps.** A chain still longer than that after 12 union passes leaves
  a region split. Unlikely, but it is a bounded approximation.
- **IDs stay sparse** — raw root pixel indices. Invisible in the preview because it hashes, but
  anything indexing an array by ID needs the collapse step first.
- **Iteration count is fixed at compile time.** Whether 12 union passes suffice at 4K, and whether
  the count should scale with resolution the way craquelure's does, is untested.

### Micro and macro

Two scales is **a second instance at a larger threshold** — wider bands, larger regions, same
filter, no second algorithm. Worth confirming in practice whether the two nest cleanly enough to
sum their variation without boundary disagreement; bands are not strictly hierarchical, so a
coarse region can in principle straddle a fine boundary.

---

## 2. Random Value from IDs — Mask

**Prototype:** `random_grayscale_fromids.cl`

Takes an ID map, emits 0…1. Every pixel in a region gets the same value; different regions get
different values. This **is** coverage, blends through `BlendMode` and `Weight` like any other
mask, and is therefore a Mask that happens to source from a Filter rather than from a texture.

### How it works

The ID goes in as the hash seed:

```c
seed += @seed_layer;
seed = SYSwang_inthash(seed);
```

Several `SYSwang_inthash` rounds chain the parameter seed, optional per-pixel coordinates, the id
and a time offset, so correlated ids decorrelate properly.

`zeroinvalid` handles `id < 0` — the "not part of any region" sentinel — by emitting 0 rather than
a random value.

### Three range modes

| Mode | Behaviour |
|---|---|
| **Uniform** | random in `[min, max]`. `min_layer` / `max_layer` can override those **per pixel**, so the variation *amount* can itself be masked |
| **Ramp** | random remapped through a ramp, so the distribution is shaped — most regions near the mean, a few outliers. Much better than uniform for subtle work |
| **Discrete** | CDF sampling from a weighted table: "60% of bricks this value, 30% that, 10% rare" |

Mode 2's spatially varying range is worth noting — "vary a lot here, barely at all there" without
a second node.

### What it's for

Any existing consumer of a mask. Stain only the recessed regions, wear only the proud ones,
chipping only on the large fragments — region-aware placement for effects that already exist,
with no new work on their side.

---

## 3. HSV from IDs — Filter

**Prototype:** `hsv_from_ids.cl`

Takes albedo plus an ID map, emits albedo. Rewrites colour rather than selecting coverage, and
runs before the composite — so, Filter.

### Two stages, and the first is the one that matters

**Palette tint.** `c = mix(albedo, color_ramp(rc), mixv)` — each region samples a *colour* from an
authored `float3` ramp and blends toward it by a random amount between `ramp_mix_min` and
`ramp_mix_max`.

This is **directed** variation. Pure HSV jitter can only wander from wherever the texture already
sits; the ramp lets you aim variation at a palette you chose — brick reds through ochres, or the
green-to-grey of weathered copper — and then jitter around it.

**HSV jitter**, applied on top: hue added, saturation and value multiplied.

### Five randoms from one ID

`s = id ^ (seed × 2246822519)`, then five different XOR salts produce ramp lookup, ramp mix, hue,
saturation and value. One hash function, five constants, fully decorrelated.

### Parameters

| Parameter | Notes |
|---|---|
| `color_ramp` | the palette regions draw from |
| `ramp_mix_min` / `max` | how far a region tints toward its sampled colour |
| `hue_min` / `max` | **−1…1 → ±180°** — the same convention as `FMixtormatLayer::HueShift` |
| `sat_min` / `max` | **multipliers** around 1, default 0.9–1.1 |
| `val_min` / `max` | multipliers, default 0.9–1.1 |
| `seed` | |

Hue is additive because it is circular; saturation and value are multiplicative because they are
magnitudes. Ranges are min/max pairs rather than ± amounts, so variation can be **biased** —
`hue_min = 0, hue_max = 0.1` shifts only warm.

Keep amounts small. A couple of percent of hue is already clearly visible on a flat surface. The
target is "same kiln, different firing", not a rainbow.

### Gap in the prototype

No `id < 0` guard. Pixels outside any region arrive as `−1`, which still hashes to something, so
they get tinted instead of passing through. `random_grayscale_fromids.cl` handles this with
`zeroinvalid`; this kernel should do the same.

---

## 4. Ramp / UVs from IDs — Filter

**Prototype:** `ramp_uvs_from_ids.cl`

Gives every region its **own local coordinate frame**, and a gradient across it. Emits two things:
a `float2` UV field and a `float` grayscale ramp. Both are per-region and both are randomised
independently per ID.

This is the equivalent of Substance's *Flood Fill to Gradient*, and it is what turns a flat ID map
into something that can place, rotate and tilt things.

### How it works

Three stages, again driven by `@Iteration`:

| Iteration | Stage |
|---|---|
| 0 | initialise four per-region bbox buffers to sentinels |
| 1 | atomic min/max of each pixel's offset from its region root → per-region bounding box |
| 2+ | derive the local UV, apply the per-region transform, emit `uv` and `gray` |

**The bounding box is built in root-relative coordinates, and that is what makes it wrap.** The ID
*is* the root pixel's linear index, so `ax = iid % xres`, `ay = iid / xres` recovers the root's
position. `wrapdelta` then measures each pixel's offset from that root the short way round the
torus. Taking min/max over *deltas* rather than absolute coordinates means a region straddling the
seam gets a small correct bbox instead of one spanning the whole image.

The local UV is then `(delta − min) / (max − min)`, clamped — a normalised 0…1 square per region.

**Per-region transform**, all hashed off the ID with separate salts:

- `ang` — full 2π random rotation when `rotate_random`
- `sc` — random scale between `scale_min` / `scale_max`
- `bs` — random bias added to the ramp

Rotation happens about the region's centre after scaling, then `tilecoord` tiles the result by
`tile_scale_x/y`, with optional mirroring on odd cells so tiled detail doesn't show a hard repeat.

**Outputs:**

- **`uv`** — the local frame, for sampling anything through
- **`gray`** — `clamp(ux + bias)`, optionally inverted: a linear gradient across each region,
  running along whatever direction the random rotation chose

### What it unlocks

**Per-region material mapping and warping.** Sample a detail texture, a noise, or a whole material
through the local UV instead of the global one. Each region then gets its own placement, rotation
and scale of that detail — so a brick wall stops showing the same speckle pattern on every brick,
and a plank floor stops sharing one continuous grain across every board. This is the single
biggest anti-repetition win available from an ID map.

**Multiply the height map by the grayscale → per-region tilt.** Each region's height gets a linear
gradient across it, oriented randomly. Tiles and shards stop lying flat in the same plane and
start reading as individually settled, lifted or sunken. This is exactly what Flood Fill to
Gradient is used for on tile and cobble materials, and it costs one multiply.

**Ramped colour per region.** Feed `gray` through a colour ramp for a gradient that runs across
each region individually rather than across the whole surface.

**As a mask.** `gray` is 0…1, so it can drive any existing effect with a per-region gradient —
wear that fades across each piece rather than uniformly.

### Category

Filter, because `uv` is a coordinate field — data, not coverage, and no more blendable than an ID
map. The `gray` half *is* mask-shaped, so a variant that emits only that could reasonably be a
Mask; simplest is one Filter emitting both, with the mask chain reading `gray` if it wants it.

### ⚠ This conflicts with Collapse IDs

`ramp_uvs_from_ids.cl` derives the region root from the ID itself:

```c
int ax = iid % @xres;
int ay = iid / @xres;
```

That only works because an ID **is** a pixel index. Collapsing IDs to a dense `0…N−1` range
destroys that, and this kernel silently produces garbage — the "root" lands at an arbitrary
position and every bbox and UV is wrong.

So the two features pull in opposite directions:

- **Collapse IDs** wants dense indices, for anything that arrays or hashes by ID
- **Ramp / UVs** wants the raw root index, because it carries the position for free

Resolutions, in rough order of preference:

1. **Don't collapse.** Sparse root indices are fine for hashing, which is all features 2 and 3 do.
   Collapse only matters if something wants to index an array by ID
2. **Carry the root position alongside** a collapsed ID — an extra `int2` or a packed channel — so
   the position survives compaction
3. **Compute true centroids** in a separate pass and store those, which is better anyway (see
   below) and removes the dependency on the ID's numeric value entirely

Whichever is chosen, it has to be decided **before** either feature is built, because it changes
what the cluster filter's output has to contain.

### Limits of the prototype

- **Bounding box, not centroid, and axis-aligned.** A long diagonal region gets a UV square that
  doesn't follow its own orientation, so a gradient across it runs at an angle to the shape. A
  centroid plus a second moment would give a properly oriented frame — more work, noticeably
  better on anything elongated
- **`clamp(@id, 0, count-1)`** means the `−1` no-region sentinel silently becomes region 0 rather
  than being excluded. Same gap as `hsv_from_ids.cl`
- The bbox buffers are four full-resolution int layers used as scatter targets — the same raw
  atomic plumbing the cluster filter needs, at four times the footprint

---

## Extend ColorId rather than duplicating it

`FMixtormatColorIdMask` already selects regions of an ID map by colour, with tolerance and
softness. It is the third natural consumer of the cluster filter, and it already exists.

The only thing in its way: `IdTexture` is a `TSoftObjectPtr<UTexture2D>` — an asset. A live filter
produces a transient RDG texture and the two don't connect. Give it a **source switch** — asset
texture *or* the layer's cluster filter — and the cluster feeds three consumers without
duplicating anything.

The alternative is baking the ID map through `MixtormatBakeService`, which already writes
`SRGB=false` / `TC_Masks` and would work with ColorId unchanged today. Not live, but instantly
reusable, and worth deciding before the port rather than during.

---

## Later, from the same ID map

Documented so the design leaves room, not proposed for the first pass.

**True centroids and oriented frames.** Feature 4 delivers the local-frame idea using an
axis-aligned bounding box. A centroid plus a second moment would give each region a frame aligned
to its *own* orientation, so gradients and mapped detail follow an elongated shape rather than
cutting across it. It would also decouple the frame from the ID's numeric value, which resolves
the Collapse IDs conflict above.

`random_grayscale_fromids.cl` already binds `centerx` and `centery` and never reads them, so the
hook is half-placed.

Radial distance from a centroid is the other thing this buys — shading a region from its centre
outward, for worn middles or darkened edges.

**Statistics by ID.** Count, sum, min and max per region turn into variation that *follows the
material*: tint by how proud a region sits, by how busy it is, by its area, by its elongation. A
hash gives variety; a statistic gives variety that looks like it was caused by something.

**Boundary and adjacency.** The ID map's discontinuities are the region boundaries for free —
per-region bevels and edge wear. Neighbour IDs give adjacency, which is what a proper macro
grouping would flood over, grouping regions that actually *touch* rather than merely sit near each
other.

---

## Open questions for implementation

- **Union-find needs raw buffer atomics.** `atomic_min` and `atomic_cmpxchg` on a global int
  buffer. `MixtormatGpuCompositor` currently binds **only textures** — there is no
  `SHADER_PARAMETER_RDG_BUFFER` anywhere in it. This is the one piece of genuinely new plumbing,
  and it is the same class of binding work that produced the `ResolveCS` error that `dxc` could
  not catch.
- **Caching.** Segmentation is expensive and its inputs change rarely; the variation amounts
  change constantly. Hash the segmentation parameters into a key like `MixtormatNetworkKey` and
  keep the HSV and random-value amounts *out* of it, or every hue nudge re-segments. Same lesson
  as moving `Warp` out of the craquelure key.
- **Where Filter sits in the child order.** Before the mask chain, since masks may consume its
  output; before the composite, since HSV rewrites albedo. Whether one layer may hold several
  filters, and whether their order matters, needs deciding.
- **Filter output lifetime.** A cluster filter's ID map has to stay live long enough for a mask
  child and a colour filter in the same layer to read it. Craquelure already does something
  similar by handing `CraqDistance` to a later pass, so there is a pattern to copy.

---

# Implementation map

Every file a new child type touches, derived by tracing `Craquelure` — the closest existing
analogue, and the one to copy. Line numbers are from the state this note was written in and will
drift; the symbol names are the reliable anchor.

## Runtime — the data

**`Source/MixtormatRuntime/Public/MixtormatMaterial.h`**
- `EMixtormatLayerChildType` (~L1116) — add `Filter`. Currently `Mask`, `Effect`, `Generated`,
  `Craquelure`, `ColorId`
- `FMixtormatLayerChild` (~L1131) — add the filter member with
  `meta = (EditCondition = "Type == EMixtormatLayerChildType::Filter")`, mirroring the existing
  five
- New `USTRUCT`s: `FMixtormatClusterFilter`, `FMixtormatHsvIdFilter`, `FMixtormatRandomIdMask`.
  Model them on `FMixtormatCraquelure` — note it was recently consolidated from 35 parameters to
  26 and adopted `FMixtormatMaskShaping`; do not repeat the sprawl
- `FMixtormatColorIdMask` — add the source switch (asset texture *or* live filter output)

**`Source/MixtormatRuntime/Public/MixtormatMaskShaping.h`**
- `FMixtormatMaskShaping` is the shared Invert / Balance / Contrast / Offset block. The
  random-value mask should embed it rather than declaring its own, which is the mistake the other
  three mask types made

## Shaders

**`Shaders/Private/`** — four new `.usf` files, ported from the prototypes in this folder:
`FINAL_IMPLEMENTATION.cl`, `random_grayscale_fromids.cl`, `hsv_from_ids.cl`,
`ramp_uvs_from_ids.cl`. `MixtormatCraquelureGrow.usf` is the model for a multi-entry-point
iterative kernel.

**`Shaders/Private/MixtormatMaskOps.ush`** — `MixtormatShapeMask` is the shared shaping function
the random-value mask should call, not reimplement.

## Compositor — the work

**`Source/MixtormatShaders/Private/MixtormatGpuCompositor.cpp`** — the bulk of it:
- shader parameter structs, near the existing `FMixtormatCraquelure*CS` declarations. **Every
  struct compiling a multi-entry-point `.usf` must declare every uniform that file references at
  scope**, not only the ones its own entry point uses — this is what caused the `ResolveCS`
  binding failure, and `dxc` cannot catch it
- the child gather loop — `EMixtormatLayerChildType::Craquelure` appears 3× there; the new type
  needs the same
- `FChildRenderData` / a new `FFilterRenderData`, alongside `FCraquelureRenderData`
- the RDG graph build, following how `CraqDistance` is produced then handed to a later pass
- `MixtormatNetworkKey` (~L1953) — hash the **segmentation** parameters only. Keep HSV and
  random-value amounts out, or every colour nudge re-segments. Same lesson as moving `Warp` out of
  the craquelure key
- **New plumbing:** union-find needs `atomic_min` / `atomic_cmpxchg` on a global int buffer. There
  is currently no `SHADER_PARAMETER_RDG_BUFFER` anywhere in this file — everything is a texture

**`Source/MixtormatShaders/Public/MixtormatGpuCompositor.h`**
- the network cache declaration (~L69) if filter output is cached the way craquelure networks are

## Editor — the UI

**`Source/MixtormatEditor/Private/Widgets/SMixtormat.h`** (~L130–140) — mirror the craquelure set:
`AddFilterToLayer`, `GetSelectedFilter` (const and non-const), `BuildFilterControls`, plus any
menu builders.

**`Source/MixtormatEditor/Private/Widgets/SMixtormat_Inspector.cpp`**
- a `BuildFilterControls()` panel. `BuildCraquelureControls()` (~L1219) is the template
- use `AddMaskShapingRows()` for the shared shaping block rather than hand-writing the rows

**`Source/MixtormatEditor/Private/Widgets/SMixtormat_Layers.cpp`** — the busiest editor file, ~8
separate spots: the add-child menu, child display name (~L1108), selection handling (~L270, L1139,
L1259), the selected-maps label (~L381), and enabled state (~L1347).

**`Source/MixtormatEditor/Private/UI/Layers/MixtormatLayerBadges.cpp`**
- `ChildKind` badge text (~L152) — `CRAQ` is the precedent, so something like `FILT`
- blend-mode badge (~L135) if the filter carries one

**`Source/MixtormatEditor/Private/Widgets/SMixtormat.cpp`** (~L545) — the per-child disable path.

## Tests

**`Source/MixtormatEditor/Private/Tests/MixtormatCompositorTests.cpp`** — `Craquelure` appears 3×;
add coverage for the new type the same way.

**`Source/MixtormatEditor/Private/Tests/MixtormatLayerPreviewTests.cpp`** — child-type coverage.

## Build and verification

`BuildEditor.bat` refuses to run while Unreal is open. Use:

```
& "<UE_ROOT>\Engine\Build\BatchFiles\Build.bat" MatLabEditor Win64 Development -Project="<PROJECT>\MatLab.uproject" -NoHotReloadFromIDE -WaitMutex -FromMsBuild
```

Compile and UHT run; only the final `LNK1104` fails while the editor holds the DLLs, which is the
expected ending. Adding a `USTRUCT` **requires** this — UHT is the only thing that validates new
reflected types.

Shaders can be compiled standalone with the Windows SDK `dxc` (see the project memory note), but
that validates HLSL only — **not** UE's parameter binding. `recompileshaders changed` in the open
editor's console is what checks bindings.

## Suggested order

1. **Cluster filter alone**, with the debug `preview` output wired and nothing consuming it. Look
   at the ID map on real scans first — everything downstream is worthless if the regions are wrong
2. **Decide the Collapse IDs question** before building anything that consumes IDs. It changes
   what the cluster filter has to emit, and features 2/3 and feature 4 want opposite things
3. **Random value mask** — smallest consumer, and it proves the filter→mask handoff
4. **HSV filter** — the payoff for colour
5. **Ramp / UVs filter** — the payoff for shape. Wire `gray × height` first, since it is one
   multiply and the tilt effect is immediately visible; per-region material mapping after
6. **ColorId source switch** — reuse, once the live-output lifetime question is settled
