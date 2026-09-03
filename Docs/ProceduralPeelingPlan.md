# Material Lab — Procedural Peeling Plan

Status: Design only. Nothing implemented.

## Goal

Drive peeling from generated signals instead of an authored texture set: seed the peel from
noise and from the surface composited underneath — convexity, cavity, AO, height, ordered child
masks — and expose frequency, amount, intensity and type as controls rather than as a baked map
set the artist cannot change.

The authored path stays. This adds a second source for the same consumer.

## The reframe

Do not make peeling procedural. Make **map generation** procedural and leave the peeling shader
a texture consumer.

`MaterialLabPeeling.usf` calls `EvaluatePeeling` five times per pixel — centre plus four
neighbours — to build the height gradient its dynamic normal comes from. Each call is five
texture samples today, which is why five of them is affordable. Move noise evaluation inside
`EvaluatePeeling` and that noise runs five times per pixel; `MaterialLabGullyField` alone loops
up to 33 taps. The design collapses under its own gradient.

So: add passes **before** the peeling dispatch that write transient PDM/MSK/H/SDF-equivalent
targets, then run the existing peeling shader essentially unchanged.

```text
Procedural passes  →  transient PDM / MSK / H / SDF targets  →  existing MaterialLabPeeling.usf
Authored effect    →  imported PDM / MSK / H / SDF textures  →  existing MaterialLabPeeling.usf
```

Benefits:

- The working peel math is untouched. Coverage, edge, SDF occlusion, lift, thickness and the
  RNM normal path all keep behaving exactly as they do now.
- Derivative taps stay cheap, because all five read the same precomputed targets.
- Both paths converge on one consumer, so there is one place where peeling is defined.
- It is the same structural move the compositor already makes for erosion's pre-passes.

## What the authored maps actually contain

Read from `Handoff/Houdini/OpenCl/peel.cl`. The set is less independent than it looks, but it is
not a single field either.

Three inputs drive everything:

```text
seed    thresholded to mark where peeling starts
macro   low-frequency field, warps the peel boundary
micro   high-frequency field, warps it again and drives secondary relief
```

`arrival` (`T`) is a geodesic distance field grown from the thresholded seed by an eikonal
solve (`solve8`, an 8-neighbour Jacobi sweep run to convergence by a Houdini solver). Every
exported channel is then a closed-form function of `T`, `macro` and `micro`:

```text
D          = T + macro*warp + micro*micro_warp*micro_morph - front
Coverage   = 1 - smooth01(D/W * 0.5 + 0.5)
EdgeMask   = exp(-2 * (D/W)^2)
DetailMask = f(macro, micro, mode)
H          = f(Coverage, EdgeMask, DetailMask, thickness, lift, detail_strength, mode)
SDF        = 0.5 + 0.5 * D / sdf_range

PDM.r = T normalised      MSK.r = Coverage
PDM.g = macro             MSK.g = EdgeMask
PDM.b = micro             MSK.b = DetailMask
```

Two consequences for this plan:

- `PDM.g` and `PDM.b` are the macro and micro **inputs**, not derivatives of `T`. Reproducing the
  set means generating three fields, not one.
- `MSK.b` is a function of the noise fields and `mode`, not of `D`. All five gradient taps must
  see the same noise, which is a second reason to precompute into targets rather than evaluate
  inline.

`peel.cl` already implements two types. `mode == 0` is the flat chip currently shipped.
`mode == 1` is a curled variant with distinct `flap` and `fold` terms. It was never ported to
HLSL. That is the cheapest visible win in this whole feature and it is a port, not a design
problem.

## What already exists

Nothing here needs inventing from scratch.

```text
MaterialLabGully.ush              periodic hash + gradient noise with analytic derivatives,
                                  wrapped to an integer lattice so it tiles
MaterialLabCurvature.ush          signed curvature, cavity, convexity from the accumulated normal
MaterialLabGeneratedMask.usf      weighted mix of curvature / direction / AO / height, with
                                  slope-flow warping, writing one scalar mask
Compositor pre-pass machinery     multi-pass ping-pong dispatch inside a layer, established by
                                  erosion
```

The Generated Mask node is the important one. It already binds the accumulated Normal, packed
RAM and Height below the owning layer and produces exactly the weighted mix of convexity, AO and
height this feature wants. Peeling already consumes the accumulated child mask.

So the requested behaviour — "weights like convexity or masks to derive the peeling" — is
already plumbed. What is missing is that the mask currently **gates the result** rather than
**seeding the propagation**.

## Our design

Three procedural passes ahead of the existing peeling dispatch.

### 1. Noise pass

Generate `macro` and `micro` into one RG target using the periodic noise already in
`MaterialLabGully.ush`. Both must be periodic on an integer cell count so the peel tiles.
Frequency is exposed per field.

### 2. Seed and arrival pass

Seed is a threshold on a weighted mix:

```text
SeedField = w_noise    * Noise
          + w_curvature* lerp(Cavity, Convex, CurvatureBias)
          + w_ao       * (1 - AO)
          + w_height   * Height
          + w_mask     * ChildMask

Seed = SeedField > Threshold
```

That expression is the Generated Mask node's mix. Prefer reusing its evaluator over duplicating
it — a Generated Mask child placed before the Peeling child can supply the field directly, which
keeps one implementation of the signal mixing and gives the artist the existing per-signal
weights, warp and shaping for free.

Then compute distance from the seed set. **Use jump flooding, not the eikonal port.**

The eikonal sweep propagates roughly one texel per iteration, so its cost scales with the
distance the front travels. Jump flooding is `log2(N)` passes — about 11 at 2K — and its cost is
resolution-bound rather than distance-bound. That matters because the seed threshold is an
exposed control: at a high threshold only a few percent of the surface is seeded and distances
become long, which is exactly where a Jacobi sweep degrades and JFA does not.

Known properties to design around:

- JFA is **approximate**. A small fraction of texels resolve to a non-nearest seed. Usually
  invisible at these scales; `1+JFA` or `JFA+1` reduces it. Do not describe it as exact.
- JFA propagates **seed coordinates, not distances**, so the ping-pong targets are RG16F or
  RG32F, not the R16F used everywhere else in the compositor.
- Uniform speed makes the result Euclidean rather than geodesic. `peel.cl` supports a varying
  `speed` layer; its visual effect is a warped distance field, and `D` is already warped by
  `macro` and `micro` downstream. Treat the speed field as deferred, not required.

### 3. Channel pass

Evaluate `D`, `Coverage`, `EdgeMask`, `DetailMask`, `H` and `SDF` in closed form from `T`,
`macro` and `micro`, writing the same layout the authored textures use. Port `mode == 1` here.

## Tiling

This is the one part with real technical risk, and it is where to start.

The distance field must wrap. Candidate comparison needs toroidal delta:

```text
float2 d = abs(SeedPos - PixelPos);
d = min(d, Resf - d);
dist = length(d);
```

and the jump offsets themselves must wrap. Miss either and a seam runs down the tile boundary.

**Prototype a seamless toroidal distance field from a periodic noise seed before building
anything else.** If that seams, nothing downstream matters.

## Build the trivial version first

`ErosionEffectPlan.md` open item 5 records that erosion's cheap single-pass alternative was never
built, so its expensive path has nothing to be compared against. Do not repeat that here.

Baseline: smooth the seed field and use it directly as an arrival proxy — one pass, no JFA, no
propagation. It will not have the grows-outward-from-a-seed character that makes peeling read as
peeling. It gives a working end-to-end procedural path, a reference to judge JFA against, and a
fallback if the toroidal wrap fights back.

## Integration

Follow the pattern erosion established for effects with no asset:

- `FMaterialLabLayerEffect::ProceduralType` already exists and already carries
  `EMaterialLabEffectType::Peeling` as its default. A procedural peel is a Peeling child with a
  null `Effect` reference.
- `UMaterialLabEffect` supplies the effect type *and* the decode ranges. With a null asset,
  neither is available, which is fine because neither is needed.
- `DistanceRange`, `SDFRange` and `HeightRange` exist only to undo 8-bit texture encoding.
  Transient float targets have no round-trip to undo, so procedural mode must **bypass** them
  rather than pass neutral values through.

One behaviour needs an explicit decision before implementation:

`EvaluatePeeling` currently does `Coverage *= ChildMask`. If the same mask also becomes the seed
source, it is applied twice — once shaping where peeling starts, once gating the result. Choose
seed-only, gate-only, or both under separate weights. Both-with-one-weight is the wrong answer
and is what falls out if nobody decides.

## Exposed controls

Mapping the requested vocabulary onto the design:

```text
Frequency   noise cell period for the macro and micro fields; integral, so it tiles
Amount      seed threshold — how much of the surface begins peeling
            Front — how far the peel front has travelled from those seeds
Intensity   Thickness, Lift, Detail Strength; all already exposed and unchanged
Type        mode 0 flat chip / mode 1 curled flap and fold, ported from peel.cl
Weights     per-signal seed weights: noise, curvature with cavity/convex bias, AO, height, mask
```

## Cost

Per procedural peeling child, per composition:

```text
1 noise pass
1 seed pass
~11 jump-flood passes at 2K, ~12 at 4K
1 channel pass
```

That is roughly 14 dispatches **per peeling child**, where the authored path costs zero
pre-passes. Two procedural peels on one layer doubles it. This lands in a graph that erosion has
already grown by ~17 dispatches at twice composition resolution, behind a preview refresh that
flushes rendering commands on every slider change.

Measure interactive scrubbing before adding a second procedural effect type.

## Limitations

- JFA distance is approximate and Euclidean, not the geodesic field `peel.cl` produces.
- Without a speed field the propagation is isotropic; boundary variety comes from noise warping
  rather than from anisotropic growth.
- Seed placement can only reference the surface composited below the owning layer, so it cannot
  produce variation independent of that surface.
- Procedural peeling is generated per composition. It is editor-time work that bakes into the
  final maps, exactly like the rest of the compositor.

## Validation

1. A seamless tile: no discontinuity in arrival, coverage or normals across the UV boundary at
   every supported resolution.
2. Seed weights at zero with noise weight at 1 reproduce a plain noise-seeded peel.
3. Each signal weight in isolation concentrates peeling where that signal is high.
4. Threshold sweeps coverage monotonically from none to nearly all.
5. Frequency changes cell size without shifting large-scale placement.
6. Type 0 and type 1 differ visibly in edge profile, and type 0 matches the authored look.
7. An authored peeling child renders identically before and after the change.
8. Procedural and authored children coexist in one recipe and in one layer.
9. Recipes with procedural peels save, reopen and rebake with identical results.
10. Baked output matches the preview.

Do not run builds, Unreal, or tests without explicit permission.
