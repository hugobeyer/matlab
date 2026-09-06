# Craquelure review

35 parameters, 855 lines of shader across 4 files, and the most expensive node in the graph.
Findings below in the order they matter.

---

## 1. Warp: the comment is arguing against a different thing than you're asking for

`MixtormatCraquelureGrow.usf:31` says warping the skeleton "would drag cracks off the pixels they
are connected through and tear it." That is true of **one** of two possible warps, and they got
conflated:

| Warp what | Safe? |
|---|---|
| The skeleton buffer during growth — discrete lit pixels and lineage ids | **No.** Breaks 8-connectivity, genuinely tears the network |
| The **read into the finished distance field** — a continuous scalar, resampled point-wise | **Yes.** Connectivity is already baked in; nothing to tear |

You're asking for the second. The comment is defending against the first. So the fix is small,
not architectural.

**Today, per mode:**
- **Lattice** warps the UV *before* the cellular lookup. Warping a procedural field's sample
  position is post-creation in effect, so it works. This is the mode where warp behaves.
- **Propagated** warps only `FieldUV` — the stress/toughness/flow fields that *steer* growth.
  The network wanders differently, but an existing network never bends. That is exactly
  "I can't seem to warp the cracks of one type."

**Where the fix lands:** the pixel coordinate at `CraquelureGrow.usf:477`
(`CrackDistance.Load(...)`) rather than `FieldUV` at 147. Two constraints:
- `frac()` the warped UV — curl warp is periodic but can leave [0,1].
- Keep `.Load` point-sampling so `NearestId` stays exact; `Variation` keys on it and it travels
  with the crack, so per-crack variation stays correct.

**It must land at every reader of that field or none.** There are two:
`CraquelureGrow.usf:477` and `CraquelureRelief.usf:57`. Warp one and not the other and the mask
and the relief normals disagree — cracks in one place, their height in another.

Known artifact, worth accepting: you read the distance at the *source* location, so local
compression makes crack width vary with the warp's Jacobian. Lattice mode already accepts exactly
this when it warps its cellular lookup, so the two modes would finally behave the same way.

### The payoff you didn't ask for

The network is cached (`NetworkCache->Find(Crack.NetworkKey, ...)`), and `Warp`, `WarpPeriod` and
`WarpSeed` are all **in that key**. So today, touching the warp slider invalidates the cache and
regrows the entire network — every drag pays full price on the most expensive node in the graph.

Move warp to the read and it comes **out of the key**. Warping becomes a cache hit: one resolve
pass instead of a full regrow. That is most of the "too complex / too slow" feeling, and it falls
out of the same change.

### One decision only you can make

Field-warp and post-warp are **different looks**, not two implementations of one:
- field-warp — cracks wander *while growing*, the network's topology changes
- post-warp — a finished network bends, topology fixed

Keep both (renamed, e.g. Wander vs Warp) or replace field-warp with post-warp? Replacing is
simpler and gives you the cache win outright; keeping costs a parameter and leaves the slow path
in for the look it uniquely produces.

---

## 2. Scale: three controls read as "scale", two live per mode

| Control | Mode | What it scales |
|---|---|---|
| `Period` | Lattice only | cells across the UV |
| `SeedCells` | Propagated only | nucleus lattice density |
| `NoiseCells` | Propagated only | stress / toughness / flow field scale |

`Period` and `SeedCells` are the same idea under two names in two modes — that alone is worth
merging into one **Scale**, switched internally by mode.

`NoiseCells` is genuinely independent (it's the field scale relative to the seed lattice, which is
what decides whether the fields steer whole regions or roughen individual cracks). But as an
absolute cell count it fights `SeedCells` — move one and the character changes. **Express it as a
ratio of the seed lattice** and it stops being a second scale and becomes what it actually is: a
roughness/coherence dial.

### The `SeedCells` in the width formula is not a bug

`CraquelureGrow.usf:474` divides `Width` by `SeedCells` deliberately, so `Width` means "fraction
of a cell" in both modes (documented at 467). Real cost, though: **density and thickness cannot
move independently.** Coarsen the network and every crack thickens with it.

That's a tradeoff to expose, not a multiply to delete. Suggest a **Width Mode: Relative | Absolute**
toggle — relative keeps today's behaviour, absolute takes width in texture fraction.

*Which mode were you in when scale felt wrong?* Lattice and Propagated have different culprits and
I can't tell from here.

---

## 3. Cost: it's already cached, so this is a scrub-latency problem

Growth is **one full-resolution dispatch per iteration** — `for (GrowPass < GrowIterations)`,
scaled by resolution, clamped to 1024. The comment at 2888 is honest about it: ~80 texture loads
per pixel per pass, "an unbounded count at 4K is minutes."

But the result is cached on the parameter key, so you only pay on a parameter *change*. Which
means:

- **Don't touch the growth algorithm.** It isn't running when idle.
- The pain is that ~20 of the 35 parameters are in the network key, so most sliders trigger a full
  regrow mid-drag.
- Fix in order of value: **(a)** move warp out of the key (above), **(b)** grow at reduced
  resolution while a slider is being dragged and refine on release, **(c)** the resolve-only
  parameters — `Width`, `Variation`, `Balance`/`Contrast`/`Offset`, `Weight`, `BlendMode` — are
  already outside the key and should stay fast; worth confirming they don't hit the regrow path.

---

## 4. Parameter consolidation — 35 down to about 18

Grouped by what a user is actually deciding.

| Keep as-is | Merge | Into |
|---|---|---|
| `bEnabled`, `Mode`, `Seed` | `Period` + `SeedCells` | **Scale** (mode picks which) |
| `Width`, `Variation` | `NoiseCells` | **Detail** (ratio of Scale) |
| `Warp`, `WarpPeriod`, `WarpSeed` | `StressVariation` + `ToughnessVariation` | **Stress Contrast** (one dial, they're opposite ends of the same balance) |
| `Iterations` (it's the cost dial — keep it visible) | `StressGain` + `ToughnessCost` | **Fracture Bias** |
| `ReliefDepth`, `ReliefNormalStrength` | `SeedChance` + `SeedJitter` | **Density** / fold jitter into Scale's high end |
| `BlendMode`, `bInvert`, `Weight` | `Persistence` + `TurnResponse` | **Straightness** (both govern how a tip holds heading) |
| `Shaping` (the shared struct) | `FlowStrength` + `Irregularity` | **Wander** |
| | `ReliefWidth` + `ReliefProfile` | **Relief Shape** |
| | `GrowthThreshold`, `CollisionLimit` | move to an Advanced section, not removed |

`Jitter` is Lattice-only and `SeedJitter` is Propagated-only — same concept, two names, same merge
as `Period`/`SeedCells`.

That lands around 18 visible controls with an Advanced fold for the growth-tuning internals, and
nothing is lost — every merged pair is two ends of one decision.

---

## Suggested order

1. **Post-warp + take it out of the network key.** Small, fixes the complaint, and makes warping
   interactive. Needs the Relief reader changed in the same pass.
2. **Merge `Period`/`SeedCells` and `Jitter`/`SeedJitter`.** Pure rename plus a mode switch; no
   behaviour change.
3. **`NoiseCells` as a ratio.** One line, removes the fighting-scales feel.
4. **The rest of the consolidation**, which is UI and data layout, not shader work.

Nothing here needs the growth kernel rewritten.
