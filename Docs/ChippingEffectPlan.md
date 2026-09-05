# Chipping

Port of `Handoff/Houdini/OpenCl/brickschips.cl`, a working 229-line prototype. Chips are seeded
from a smooth height selection mixed with local cavity, then grown inward over N iterations,
carving the composited height and exposing a substrate colour underneath.

## The audit was wrong about the blocker

`EffectOpportunityAudit.md` filed chipping under "blocked on one primitive" alongside craquelure,
on the assumption it needed a Voronoi lattice. Reading the prototype's bindings, it does not:

```
#bind layer height float border=WRAP
#bind layer layers float border=WRAP
```

Bricks come from thresholding the **composited height** at `grout_level`, not from a synthetic
cell lattice. That is strictly better for us. Chipping works on whatever the user actually built
-- brick from a tiled texture, planks from a height map, craquelure output itself -- rather than
only on a lattice we generate. `MixtormatCellular.ush` is not a dependency, and chipping was
never blocked behind it.

## Class

`Filter`, alongside Erosion and Grade. It reads the height the layer just composited, modifies
it in place, and derives its colour and normal change from what it removed. Same slot in the
graph as erosion, and it runs after it -- carving a surface that has already weathered is the
order that makes sense, and the reverse would have erosion smoothing chips it never saw.

## What the prototype does, and what each piece becomes

| Prototype | Here | Note |
|---|---|---|
| `brickmask(h, level, soft)` | same | `smoothstep(level, level+soft, h)` on the composited height. This is the whole brick/grout split. |
| `edge` | seed boost | Brick pixels adjacent to grout remain preferred, but smooth height mixed with cavity can seed away from a hard boundary. |
| `inward` | same | Gradient of the brick mask, pointing into the brick. Chips travel inward from the edge. |
| `downhill`, `curl` | same | Height gradient and a divergence-free noise field. `irregularity` weights curl against inward. |
| `@state` float4 | `PF_A32B32G32R32F` | See precision, below. |
| `@Iteration == 0` | `Iteration` uniform | Load-bearing branch, not an optimisation. See below. |
| `@WRITEBACK` scratch -> state | RDG ping-pong on parity | Same shape as `EroH[2]`. |
| `@layers` | the layer's `CombinedMask` | See below. |
| `@chips` output | drives the shade pass | Coverage, directly. Not reconstructed from a height difference. |

## Three decisions

**State is `PF_A32B32G32R32F`, not `PF_FloatRGBA`.** The same argument the erosion height chain
carries at `MixtormatGpuCompositor.cpp:2192`. `tip` is a geometric decay -- multiplied by
`decay * weak` (0.72 to 0.99) every iteration, read back, re-multiplied -- and compared against a
hard `0.001` cutoff. Half-float quantisation on a value approaching that cutoff turns "the chip
stops here" into a per-pixel coin flip, which is the dithering the erosion precision pass just
fixed. The stored direction is worse: it is renormalised each iteration and fed to a hard
alignment test at `dot > -0.10`, so quantisation error compounds into the branch itself.

**`@layers` becomes the layer's own mask, under an explicit control.** In Houdini that input is a
material-id map whose gradient marks where one painted material meets another; `layeredge` then
boosts chip seeding, survival and depth at those boundaries. The closest thing here is the
layer's accumulated mask -- its gradient marks the boundary of where this material was painted,
which is the same idea. It is bound to `CombinedMask` and scaled by `Chip Mask Edge`, defaulting
to 0 so the port's behaviour is opt-in rather than assumed. With no mask the layer's mask is
uniform, its gradient is zero, and the term is inert either way.

**Iterations scale with resolution.** Chips propagate one pixel per iteration, so a fixed count
would make chip size a fraction of the texture that changes between a 512 preview and a 2048
export -- the preview would lie. The count is multiplied by `Resolution / 1024` and clamped, so
reach is constant in UV. A dilated gather would have been cheaper and is the obvious alternative,
but at stride 2 the four pixel-parity classes never read each other and you get four independent
interleaved chip networks. Cost scaling with resolution is the correct answer here.

## The iteration 0 branch

The first dispatch is genuinely a different dispatch:

- `old` is zero rather than read
- `seedtip` is computed; on every later pass it is zero
- the 3x3 propagation gather does not run
- `keepdir` falls through to `field` unconditionally, where later passes keep `old.zw` when
  `tip <= 0.001`

Get that last one wrong and every chip inherits a zero direction on iteration 1, the alignment
test fails everywhere, and nothing propagates. It is a uniform rather than a permutation because
the divergence is uniform across the dispatch.

## Shade

`MixtormatErosionShade.usf` already solved "carve depth drives colour and roughness", so it is
renamed `MixtormatCarveShade.usf` and generalised rather than duplicated. It grows a coverage
texture input and a `UseCoverageTexture` switch:

- Erosion binds a dummy and 0, keeping `Coverage = saturate((Src - Carved) / CarveDepth)`
- Chipping binds its chip mask and 1

Chipping already computes coverage directly as `depthmod`, which is a better input than a
reconstructed difference: at chip depths around 0.035 the height difference is small enough that
deriving coverage from it reintroduces exactly the cancellation the R32F work removed.

## Two things the prototype got away with and this cannot

**`Amount = 0` had to become the identity.** The prototype's seeding threshold tops out at
`0.985 - Amount*0.30`, and the noise it is compared against is in [0,1), so at Amount 0 a
fraction of pixels still seed and still carve. Fine in Houdini, wrong here: Grade's own tooltip
states the contract out loud, and this is verification item 1. Anchored at 1.0 with the span
widened to 0.315 to compensate, so a threshold of exactly 1 is unreachable at 0 and the default
sits where it did. The dispatch loop is skipped entirely at 0 as well.

**The inward direction needed its own epsilon.** The composited height is `PF_R16F`
(`MixtormatGpuCompositor.cpp:932`). Near a grout level of 0.15 a half ULP is about 1.2e-4, and
`BrickMask` amplifies by `1/GroutSoftness` -- 12.5 at the default -- putting roughly 1.5e-3 of
noise on the mask. In a flat brick interior the true mask gradient is zero, so a generic 1e-7
normalize floor passes that noise straight through as a random unit vector, weighted 1.30 in
`Field`. At float32 the prototype's noise sits below its floor, `Inward` returns zero, and the
interior gets the coherent `Downhill + Curl` field instead -- so on R16F, `Irregularity` would
have stopped meaning what it means. Floored at 5e-3, two orders below a real edge gradient of
about 0.5 per pixel. Widening the chipping height chain cannot fix this: the quantisation
happened upstream, in the target the composite writes.

## Two bugs the port carried over, found on first use

**`Size` did nothing, because three variation terms were inside the recurrence.**
`Best = s.y * w * nb * bm` where `s.y` is the neighbour's tip, so the propagation was

```
Tip[n] = Tip[n-1] * alignment * hash * Weak * Decay
```

`Decay` is the term `Size` controls; the other three are shaping. Compounded per pass they came
to about 0.85^n and swamped it -- across its whole range `Size` moved a chip from 2 pixels to 3.
The alignment weight is the worst offender and its cost is structural: there are only eight
neighbour directions, so even a perfectly followed front can do no better than cos(22.5) = 0.924
alignment, which the curve turns into a 0.848 multiplier charged every single pass.

Fixed by separating selection from amplitude. `BestScore` still carries alignment and the
per-neighbour hash and decides *which* neighbour a pixel inherits from; `Best` is what actually
propagates and carries neither. `Weak` moved to the seed, where a variation term belongs and
where it no longer compounds -- it was also double-applying `n1`, which already varies depth in
`DepthMod`.

| Size | before | after |
|---|---|---|
| 0.0 | 0.479/pass, 2 px | 0.820/pass, 7 px |
| 0.6 | 0.540/pass, 2 px | 0.925/pass, 16 px |
| 1.0 | 0.581/pass, 3 px | 0.995/pass, 244 px |

`Iterations` is now the real cap rather than a control that cost dispatches and bought nothing,
so its default rose from 10 to 16 -- below that it, and not `Size`, decides how big chips get.

**Placement had no control at all.** Seeding was gated by an invisible hardcoded 24-cell noise
field intersected with a thin edge band: at the default `Amount` only 4.6% of that field clears
the threshold, so chips came out as isolated specks with nothing the user could aim. The layer
mask was bound but only its *gradient* was read, under `Mask Edge`, which defaults to 0 -- so
painting a mask did nothing.

`SeedTip` is gated by a placement mask. Chipping can own a mask selected in its inspector;
when unset it falls back to the layer's accumulated child mask. This hands chipping the entire
mask vocabulary without forcing it to share placement with the layer itself.

Deliberately not done in the same pass: exposing the hidden noise periods (24 placement, 53
depth, 17 curl) as one scatter control. Worth having, but the reach fix changes what the field
looks like enough that its default should be chosen afterward rather than guessed now.

## Controls

| Control | Range | Default |
|---|---|---|
| Amount | 0..1 | 0.45 |
| Grout Level | 0..1 | 0.5 |
| Grout Softness | 0..0.5 | 0.08 |
| Cavity Influence / Offset | 0..1 / -1..1 | 0.5 / 0.0 |
| Cavity In Low / In High | -1..1 | 0.0 / 0.04 |
| Height Influence / Contrast | 0..1 / 0.1..8 | 1.0 / 1.0 |
| Placement Mask / Tiling / Invert | — / 1..16 / bool | Child Mask / 1 / false |
| Size | 0..1 | 0.6 | (~7px to ~240px of reach)
| Depth | 0..0.25 | 0.035 |
| Irregularity | 0..1 | 0.6 |
| Iterations | 1..32 | 16 |
| Normal Strength | 0..32 | 8.0 |
| Mask Edge | 0..1 | 0.0 |
| Seed | 0..64 | 1 |
| Colour / Colour Amount / Roughness | — | grey, 0.0, 0.0 |

Both shade amounts default to 0, matching erosion: at zero the shade pass is skipped entirely,
so the common case pays nothing for a feature it is not using.

## The normal pass

Not in the prototype, and chipping is invisible under lighting without it -- carving the height
alone changes nothing a renderer reads. One dispatch after the loop turns the finished chip mask
into a normal and reorients the composited one against it, the same RNM combine the erosion
normal pass uses.

It Sobels the chip mask rather than a height difference, which is the one place it differs from
erosion and is strictly better: the mask is the carve shape already, at full range, so nothing
has to be recovered by differencing two nearly equal heights. Depth then scales it, so the
normal tracks the control rather than the height chain's precision.

## Verification

1. `Amount = 0` -- height must come out bit-identical to the input.
2. Grout Level swept across a height field: chips follow the smooth height selection and cavity,
   never propagating below the grout transition.
3. `Amount = 1` over craquelure relief with an owned white placement mask -- some pixels
   must be carved.
4. `Irregularity 0` vs `1` -- straight inward chips vs wandering ones.
5. Iterations 1 -- seeds only, no propagation. Iterations 24 -- chips reach far but stay bounded.
6. Same recipe at 512 and 2048: chip size relative to the texture must match.
7. Chipped colour at Amount 1 with Depth 0 -- colour must still appear, since coverage is the
   chip mask and not a height difference.
8. Erosion's shade unchanged after the rename: an eroding layer with a colour set must look the
   same as before.
9. A grade with an asset-backed effect still hides the asset panel -- the visibility test moved
   from naming Erosion to testing the effect class, which fixes a latent gap on Grade.
