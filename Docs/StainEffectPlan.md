# Stain


## Gravity

Stains now run down the V axis the way liquid runs down a wall, over a distance `Height Warp`
sets. `Gravity` is signed and defaults to 1.

**Why it needed to be a gather, not a displacement.** The existing warp traces the source uphill
so the visible stain lands downhill -- that *moves* a stain, it does not lengthen it. A drip is a
trail: every pixel below a source has to inherit that source. So `GatherStainMask` marches
against gravity from each pixel and takes the strongest mask it finds, with a squared falloff so
a run thins as it descends instead of ending square. Marching from the pixel is equivalent to
smearing every source downward, without a second buffer.

**Why it is not slope-gated.** The height warp multiplies by `Slope`, so on a flat wall it does
nothing -- and a flat wall is precisely the surface this is named for. Gravity bypasses that gate
or the feature does not exist where it is wanted.

**Why the step count is derived, not fixed.** Reach is `Warp * |Gravity| * 0.20`, a fifth of the
texture at full Warp. A fixed step count would space the taps by the reach itself, so a long run
would sample every Nth pixel and come out dashed -- a mask feature between two taps is never
seen. Steps are one per pixel of reach, capped at 64; at the cap the spacing is about six pixels
at 2K, below any feature worth drawing a drip from.

**Why it is signed rather than tied to `FlipNormalY`.** That flag is about normal-map
green-channel encoding. Whether +V is down depends on how the mesh was unwrapped, which is
independent of it, and coupling them would flip every drip on a normal-map re-import.

**`Height Warp` changes meaning.** It used to buy at most 0.025 UV of displacement, slope-gated.
With gravity on it reaches roughly twenty times further. Existing stains with Warp turned up will
look different; Warp defaults to 0, so most will not.

## Clamping policy

The gather used to clamp most authored scalars to the range its slider showed, so typing a value
above the slider silently did nothing. A clamp there now has to guard an invariant the code
cannot survive without -- a loop bound, a dispatch count, a shift width, a decay that has to stay
below 1 -- and everything a slider merely has an opinion about passes through.

Where an invariant was being protected by clamping the control, the guard moved onto the
invariant instead:

- chipping's `Decay` is capped just under 1 rather than `Size` capped at 1, because at a
  retention of 1 a tip never decays and every chip grows to the iteration cap
- chipping's alignment exponent is floored at 0.05 rather than `Irregularity` clamped, because
  past 1 the exponent goes negative and `pow(0, negative)` is infinite

`ClampMin`/`ClampMax` metadata came off the same properties, since a details-panel edit would
otherwise re-impose the ceiling through a different door.
