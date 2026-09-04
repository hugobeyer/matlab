# Material Lab — Erosion Effect Plan

Status: Implemented and building. Not visually verified in the editor.

## Goal

Add an ordered layer effect that reshapes the height accumulated below its owning layer,
carving slope-aligned gullies so harsh height transitions gain believable relief instead of
reading as steps. It also produces a ridge map, which becomes a new signal for generated masks.

## Independent implementation

Material Lab implements this technique itself. Rune Skovbo Johansen's Advanced Terrain Erosion
Filter (Shadertoy `sf23W1`, recorded in `shadertoyerosion.md`) is the reference for the idea,
not a source to port. That shader is itself the third in a line — Clay John, then Fewes, then
Rune, whose header notes his version retains little beyond the high-level concept.

The concept, which is what carries over:

```text
Carve gullies whose direction follows the local downhill slope.
Stack them across octaves, each octave steering the next.
Gate carving by slope, so flat ground is left alone.
Track where gullies converge to produce a ridge and drainage map.
```

What must not carry over is the expression: loop structure, parameter set, variable roles and
naming. A renamed paraphrase is a derivative work and MPL-2.0 would still attach. Our
implementation is written from the description above and from our own constraints, which differ
enough that a faithful port would be the wrong shape regardless.

Credit Rune Skovbo Johansen as the inspiration in the plugin documentation. This is not a
license obligation under an independent implementation; it is ordinary practice.

Do not copy `PhacelleNoise` or `ErosionFilter` into the plugin. Do not keep a copy of
`shadertoyerosion.md` in shipped content; it is a reference document and stays in `Docs/`.

## Why our constraints force a different design

```text
Terrain reference                     Material Lab
never repeats                         must tile exactly on an integer period
generates its own fBm heightfield     consumes composited layer height
uniform parameters across the frame   parameters vary per pixel from the mask stack
one full-screen pass, real time       one pass per recipe change, editor time
screen-space domain                   UV domain, resolution independent at 1K/2K/4K
```

Tiling is the deepest of these. A cell lattice that closes on a period changes how cells are
laid out and hashed, so it is a different noise construction rather than the same one with
wrapped inputs. Design for the period from the start instead of retrofitting a wrap.

The editor-time budget cuts the other way and is worth exploiting: cost is paid once per recipe
change, not per frame, so we can afford more per pixel than a real-time shader would.

## Our design

A directional gully field evaluated per pixel over a bounded octave loop.

Each octave:

```text
1. Read the current downhill direction from the working slope.
2. Evaluate a periodic gully field oriented to that direction, returning
   a signed offset and its analytic derivative.
3. Weight it by a slope gate, so flat regions receive nothing.
4. Accumulate offset into height and derivative into slope.
5. Steer the next octave with the updated slope; scale frequency and
   amplitude by the octave falloff.
```

Requirements on the gully field:

- Periodic on an integer cell count so the result tiles at every supported resolution.
- Oriented by an arbitrary 2D direction, not axis aligned.
- Returns an analytic derivative, so slope stays exact without extra sampling.
- Deterministic from UV alone: no time, no screen space, no frame history.

Ridge and drainage accumulate alongside, tracking where successive octaves agree.

The slope gate is what keeps this from being a blur. Carving scales with existing slope, so a
harsh transition gains relief while flat ground is untouched by construction.

## Inputs

The filter needs height plus analytic slope. Both already exist below the owning layer:

```text
Height  HeightTargets[LayerReadIndex]
Slope   central-difference gradient of that height field
```

Take the slope from the height field rather than from the accumulated normal, so the gradient
matches the field being reshaped. Reuse the Sobel approach already in `MaterialLabCurvature.ush`.

## Tiling

This is the one part with real technical risk and the only place the algorithm itself changes.

A cell-based directional field samples an integer lattice through a hash. A terrain never
repeats, so the original has no tiling requirement. A Material Lab surface must tile exactly.

The lattice coordinate must be wrapped to an integer period before hashing:

```text
gridPoint = fmod(gridPoint + Period, Period)
```

`Period` must equal the cell count across one UV repeat, so `freq`, `cellScale` and `scale` are
no longer free: they must resolve to a whole number of cells. Octaves multiply `freq` by
`lacunarity`, so every octave's period must also stay integral. Restricting `lacunarity` to 2 and
choosing a power-of-two base period is the straightforward way to guarantee this.

Prototype the wrap on compute.toys before porting. If seams persist, the fallback is to erode at
a single scale with a tileable period and accept fewer octaves.

## Implementation notes

```text
File          Shaders/Private/MaterialLabGully.ush
Octave loop   bounded, [unroll] with a compile-time maximum
Hash          Material Lab's own, shared with future noise work
Precision     R16F height target, as the compositor already uses
```

Write it against our own hash rather than borrowing one, since the hash determines the cell
layout and therefore the look. A single shared hash also serves the noise signals the generated
mask will want later, so it belongs in its own header from the start.

Keep the gully field separate from the erosion driver. The field is reusable — warping, mask
generation and any later directional-noise work can all call it — while the driver is specific
to reshaping height.

## Filter effects

Erosion is a filter, not a generator. It reads a channel that already exists and returns a
transformed version of it. Nothing is introduced that was not already there, and with its
amount at zero it is the identity.

This is a different kind of thing from the effects that exist today:

```text
Peeling, Stain    consume the mask, modify the owning layer's coverage, normal and AO
Erosion           reads a composited channel, returns a filtered version of that channel
```

Model it as an explicit category so the distinction survives contact with later work:

```text
EMaterialLabEffectClass
    Surface     writes coverage / normal / AO through EffectData   (Peeling, Stain)
    Filter      transforms one composited channel in place         (Erosion)
```

Filters share one contract. Each declares the channel it filters, is the identity at zero
amount, and must not read state produced later in its own layer. That makes the obvious
successors — height blur, slope limiting, mask dilate or erode, curvature sharpen — additions
to an existing category rather than a new special case each time.

The category also decides UI: a Filter child names the channel it acts on, so a layer carrying
three effects reads as what it does rather than as three opaque entries.

## Integration

An ordered effect child, alongside Peeling and Stain:

```text
EMaterialLabEffectType::Erosion
```

Unlike those, it has no source textures. It ships as a stub `UMaterialLabEffect` asset with
`EffectType = Erosion` so `FMaterialLabRegistry::GetEffects()` discovers it through the existing
path and it can carry authored defaults.

The effect pass currently writes coverage, normal and AO into `EffectData`, which is the
Surface contract. Erosion is the first Filter, so the dispatch gains a second path: bind the
height ping-pong targets, read the accumulated height, write the filtered result back. This is
the same structural change already made for generated masks reading the accumulated surface,
and it is the path every later filter reuses.

## Exposed controls

Ranges below are the inspector's scrub range only. No erosion control carries a hard clamp:
`ClampMin`/`ClampMax` are absent from the recipe struct and the compositor passes every value
through raw, so a typed value outside the scrub range reaches the shader intact. The shader
keeps epsilon guards at its own division sites; those are not UI limits and stay.

```text
Amount            0..1          blend against the unmodified height; 0 is the identity
Passes            1..32         carving passes; each holds one octave finer
Period            1..256        gully cells across one UV repeat at the coarsest pass
Repose            0..32         critical slope. nothing carves below it
Repose Softness   0..32         width of the transition above the critical slope
Slope Radius      1..32         pixel radius of the local slope measurement
Slope Blur        0..16         low-pass on the guidance height before measuring slope
Curvature         Mean|Valley|Ridge   which curvature the cavity gate measures
Cavity Influence -4..4          how much the cavity gate participates; 0 is the identity
Cavity Offset    -4..4          added to the curvature before the remap
Cavity In Low    -4..4          bottom of the curvature window; above In High inverts the gate
Cavity In High   -4..4          top of the curvature window
Height Influence -16..16        signed. positive erodes raised ground first, negative low ground
Height Contrast   0..32         smoothstep contrast separating high ground from low
Gully Weight      0..8          how deep each pass cuts and how strongly it steers the next
Blend Softness    0..8          crossover width of the subtractive height blend
Normal Strength   0..32         how strongly the carve perturbs the layer normal; 0 is height only
Exposed Color     swatch        the color the carve exposes
Color             0..1          how far the carve blends toward it; 0 skips the shade pass
Roughness        -1..1          signed offset on the composited roughness where the carve bit
Full At Depth     0.001..1      the carve depth that reads as fully eroded
Direction Mode    Weight|Lerp|Flow    see Tangent flow direction below
Direction Angle   0..360        authored flow direction, 90 is +Y
Direction Amount  0..1          how much the authored direction displaces the downhill flow
Flow Radius       1..64         Flow only. gradient radius the orientation field is built from
Flow Smooth       1..16         Flow only. smoothing iterations; how far a gully holds its line
```

Internal, not exposed: Strength 1.0, Gully Length 1.5, Lic Steps 5, Gain 0.5, Deriv Scale 0.6,
Deriv Min 1, Seed 1.

`Passes` and `Period` are the two exceptions to the no-clamp rule above, and they clamp in the
compositor rather than in the recipe struct. `Passes` is a GPU dispatch count, so a typed 10000
costs frames instead of producing an odd-looking surface; `Period` lays out the noise lattice.
Both clamps match their scrub ranges exactly, so for these two the scrub range is the real
limit. Every other control still passes through raw.

The octave loop is unaffected by a high `Period`: `MaxOctave` stops doubling once a cell is two
pixels wide, which at 4096 internal binds at 11 octaves from `Period = 1` and at 3 from
`Period = 256`. The loop's own `MaxOctave < 20` guard is therefore never the binding one.

`Height Contrast` is a relabel of the former `Height Scale` and keeps the field name. The
cavity pair is not a relabel — see below.

## Cavity: curvature measure, offset and remap

Cavity was a signed bias against a smoothstep contrast. Contrast around a fixed centre can only
widen or narrow the transition; it cannot say *which* curvatures count as a channel, which is
the decision the gate is actually making. It is now an offset and an explicit input window:

```text
Weight = smooth(remap(Curvature + Offset, In Low, In High))
Gate   = lerp(1, Weight, Influence)
```

`In Low` above `In High` inverts, so pointing a Valley measurement at the flats beside a channel
does not need a separate sign control. `Influence` is kept because a remap window cannot produce
a constant 1, and `Influence = 0` is what makes the gate the identity — the shipped default.

### Which curvature

The gate reads the full Hessian, not its trace. The eight taps the Sobel already loads are
exactly the stencil a mixed second derivative needs, so `Hxy` and therefore both principal
curvatures cost no extra fetches:

```text
Hxx = (h_p0 - 2h + h_m0) / sx^2
Hyy = (h_0p - 2h + h_0m) / sy^2
Hxy = (h_pp - h_pm - h_mp + h_mm) / (4 sx sy)

Mean   = (Hxx + Hyy) / 2
Valley = Mean + sqrt(((Hxx - Hyy)/2)^2 + Hxy^2)      larger principal curvature
Ridge  = -(Mean - sqrt(((Hxx - Hyy)/2)^2 + Hxy^2))   negated smaller
```

The trace is the wrong measure for a channel. An elongated one has `Hxx = k, Hyy = 0`, so the
trace reports half its depth; on a saddle `Hxx = -Hyy` cancels it entirely, and saddles are
where drainage starts. A channel crossing a broad convex form — the common case on a composited
height — nearly cancels under the trace and survives at full strength under Valley. `Ridge` is
not `Valley` inverted: the two report different eigenvalues.

Signs are the raw height-field convention, where a channel is concave up and therefore has a
positive second derivative. `MixtormatCurvature.ush` negates because it differentiates the
normal, which already points against the gradient. That negation was carried into the erosion
Laplacian in error, so positive `Cavity Bias` used to *suppress* carving in channels and promote
it on bumps — the opposite of what the control, its comment and this document all claimed. Fixed
with the rewrite; any recipe that set a non-zero Cavity Bias will read differently.

### Feedback across passes

Curvature is measured on `GuideHeight`, which is a blurred copy of the height the *previous*
pass carved, not of the source. That is the pre-existing arrangement — the Sobel reads the same
texture — and it is what lets later passes deepen the channels earlier ones cut.

Valley makes that loop stronger than the trace did. A freshly cut channel has a large positive
`Hxx` and a near-zero `Hyy`; the trace halved that, Valley does not, so the gate reopens at full
strength on the pixel it just carved. `Repose` and `Blend Softness` are the only brakes and
neither is curvature-aware. Worth checking at `Cavity Influence = 4` with `Passes = 32`, the
corner of the exposed ranges where it is most likely to run away into single-pixel slots. If it
does, the answer is a curvature-aware brake, not a narrower Influence range.

### Units

The curvature is scaled by one stencil length, turning height-per-UV-squared into height-per-UV
— the same quantity `Repose` thresholds. That is what makes an explicit window authorable: the
two gates now scrub over comparable numbers instead of the cavity one living in the thousands.
It also removes the half-normalisation this document previously recorded as a known consequence,
where a second derivative carried one factor of `Resf.x` instead of two and cavity gating
therefore weakened every time the internal resolution rose.

Full invariance is still not claimed and is not reachable here. `Slope Radius` is in pixels
while the internal resolution is `2 x composition` capped at 4096, so the stencil covers a
different fraction of the surface at different output sizes, and the window moves with both.
The slope gate has exactly the same property, which is why matching its units was the right
target rather than chasing an invariance neither gate can have.

Concretely: at 2K internal with `Slope Radius = 2`, a channel 0.2 deep and 0.02 UV wide reads
about 1.0, which is why the default window is `0..1`. Changing `Slope Radius` moves that reading
and the window needs re-scrubbing. Making the radius UV-relative would fix both gates at once
and is the obvious follow-up; it is deliberately not bundled here.

Repose is exposed because its units are height per UV, so the useful value depends entirely
on the composited height range and cannot be fixed at a prototype value.

## Internal resolution

Erosion runs at `min(2 x composition, 4096)` and resamples back to composition resolution.
Carving is high-frequency work: at composition resolution the octave loop reaches its
two-pixels-per-cell floor with passes still to run, so the finest gullies have nowhere to cut.
At 4096 composition the cap makes this a no-op and the pass falls back to a plain copy.

Resampling is one dispatch that moves height and normal together, in either direction. At 2:1
a bilinear tap at the destination texel centre lands on the 2x2 centroid, so the downsample is
an exact four-texel average. Normals are decoded, renormalised and re-encoded; renormalising
the 0..1 encoded value would aim at the corner of the unit cube rather than the surface.

The cavity signal used to be half-normalised at this resolution, so its gating weakened as
resolution rose. That is corrected — see *Cavity: curvature measure, offset and remap* above.

## Tangent flow direction

`Direction Mode = Flow`, and the same field is reused by the stain effect. Built in
`MixtormatFlow.usf`, decoded through `MixtormatFlow.ush`.

Weight and Lerp both start from the downhill gradient, which is a per-pixel quantity: on a
composited height it wanders texel to texel, which is the whole reason `Slope Blur` exists.
Blurring the height treats the symptom. Flow treats the cause — it smooths the *orientations*
instead, so a direction is stable over a region because the region agreed on it.

### Why a double angle

A tangent taken from a gradient is **undirected**. The two sides of a ridge point opposite ways,
so averaging tangent vectors cancels them along exactly the features worth following. Doubling
the angle removes the cancellation: a direction and its opposite land on the same point of the
double-angle circle, so a plain componentwise average of `(cos2t, sin2t)` is a correct average
of orientations. Halving it back recovers the dominant one. This is the structure tensor written
in two channels rather than four, and it is the whole of Kang's edge tangent flow.

```text
Build   G = Sobel(height, Radius)
        (Gx^2 - Gy^2, 2 Gx Gy) / |G|^2      unit double-angle vector, or zero when flat
Smooth  componentwise 3x3 blur, repeated Flow Smooth times
Read    Coherency = length(encoded)
        t = 0.5 * atan2(encoded.y, encoded.x)
        Tangent = (-sin t, cos t)           across the gradient, along the feature
```

### Coherency comes free

The build stores **unit** vectors, so after smoothing `length(encoded)` is already a coherency in
`0..1`: a pixel whose neighbours agreed keeps length near 1, one whose neighbours disagreed
averages toward 0. That is what removes the need for a third channel carrying a reference
magnitude, and it halves the target to RG16F.

It also removes a control. Erosion blends `lerp(downhill, tangent, Coherency)`, so incoherent
ground falls back to plain downhill on its own and flat ground behaves exactly as it did before.
Nothing needs authoring for that case.

The trap that follows from it: `Flow Smooth` floors at **1**, not 0. Before any smoothing the
stored vectors are unit length by construction, so coherency is exactly 1 wherever a gradient
exists — full confidence in a field that is still pure per-pixel noise, and the blend hands the
carve straight to it. Zero smoothing is the worst setting available, not the neutral one, so the
compositor clamps it away. One blur is the minimum that makes the length mean anything.

The tradeoff taken: weighting each vote by `|G|` would distinguish a strong edge from a weak
one, but reintroduces a scale to normalise against and therefore the third channel. Flat ground
contributes a zero vector rather than a random direction, so it lowers coherency instead of
voting, which covers the case that actually mattered.

### Orienting it

The tangent stays undirected after decoding, so it is pointed along a reference — downhill for
erosion, uphill for a stain's source trace. `sign()` is wrong for this: it returns zero exactly
where the tangent is perpendicular to the reference, which is not a measure-zero case but every
pixel along a crest or valley floor, precisely where a direction matters most. `MixtormatOrientAlong`
uses `dot >= 0` instead.

### Built once, pre-carve

Erosion builds the field from `SourceH` — the layer height before any carving — and holds it for
every pass. Rebuilding it per pass from the carved height was the alternative and is worse twice
over: it costs the build and its smoothing eight times, and it makes the field chase the channels
the filter just cut, which is the same positive feedback the Valley curvature mode already risks,
with nothing to brake it. What the field represents is the drainage structure the surface arrived
with, and that does not change as the carve deepens it.

The consequence to know: the field therefore disagrees slightly with the slope gate, which does
read the progressively carved guide. That is deliberate, and it is what keeps Flow stable.

### Reuse in stain

`MixtormatStain.usf` traces the source UV uphill so the visible stain is displaced downhill. That
trace used a two-texel gradient, which turns wherever the height has grain, so a run frayed
instead of holding a line. `Stain Flow Amount` blends that gradient toward the coherent tangent
oriented *uphill* — same field, same header, same orientation helper.

The two call sites differ in resolution, in which height they read, and in where they sit in the
graph, so what is shared is the construction — a `.ush` decode plus an `AddFlowField` lambda in
the compositor returning an `FRDGTextureRef` — not a texture. A stain builds its own field, and
only when `Flow Amount` and `Height Warp` are both non-zero; otherwise it binds the same cleared
1x1 dummy the non-flow erosion paths use, and costs nothing.

### Cost

One build plus `Flow Smooth` blur dispatches, over a ping-pong pair of RG16F targets at the
consumer's resolution: 64MB each at 4096 internal, 16MB each at composition 2048. Paid once per
erosion effect in Flow mode and once per flow-using stain, never per pass.

Several flow-using stains in one layer build a field each, since each carries its own radius and
smoothing. Their lifetimes do not overlap — a field is referenced only by its own build, its
blurs and the one stain dispatch that reads it — so RDG can free or alias each before the next
is built. Worth re-checking in a graph capture if a heavily stained recipe shows a memory spike,
because that reuse is an RDG behaviour rather than something this code enforces.

## Precision: why the height chain is R32F

Reported symptom: the carve looked steppy and dithered. The cause is that every quantity this
filter derives is a difference of two nearly equal heights, and R16F does not survive that.

A half near mid height has a ULP of `2^-11`, about `4.9e-4`. Three consequences, worst first:

**The slope gate.** The Sobel sums six taps and scales by `Res / (8R)` — 128 at 2K internal with
radius 2. Quantisation error across those taps is roughly `4 x ULP ~= 2e-3`, so the measured
slope carries about **0.25 of noise**. `Repose` defaults to 0.30 with a 0.25 transition, so the
gate is close to a coin flip per pixel — and it multiplies the carve, so the *height* comes out
dithered before anything else touches it.

`Slope Blur` cannot fix this and never could. The 9-tap average reduces the noise correctly, and
then the R16F write throws the result straight back to one ULP.

**The normal pass.** It differences the carve depth between neighbours. Those differences are far
smaller than the carve itself, so they land on nought, one or two ULP — a handful of distinct
slopes across the whole carve, amplified by `Normal Strength` at a default of 8.

**The Hessian.** A second difference divided by `StepUV^2` multiplies its input error by about a
million. Invisible so far only because `Cavity Influence = 0` bypasses the gate exactly.

`SourceH`, `EroH[2]` and `EroGuide` are therefore R32F. `EroGuide` matters as much as the rest:
it is what the slope and curvature stencils actually read. `EroRidge` stays R16F — it is a 0..1
output signal that is never differenced.

Expect the cavity remap window's useful range to shift now that the signal is clean. That is the
precision fix landing, not a regression.

The normal pass also moved from a four-tap central difference to the same 3x3 Sobel the slope
uses. A central difference is the noisiest estimator available and is axis-biased, which is the
reason `MixtormatCurvature.ush` moved off one; here it mattered more, because it is differencing
a quantity that is itself a difference. The `1/8` preserves the old scale, so `Normal Strength`
means what it meant before.

**One-preview test that separates the two.** Set `Repose = 0`. The gate is then fully open with
no threshold to dither around. If the dithering largely goes, the slope gate was dominant; if it
persists, the normal pass was.

## Exposed colour and roughness

`MixtormatErosionShade.usf`. Erosion is a post-layer filter, so by the time it runs the layer has
already composited its base colour and RAM. Rather than route a coverage signal back through the
composite and reorder the filter, the shade pass reads what the composite wrote and blends in
place — the same shape the height and normal writes already have.

```text
Carve    = max(SourceH - Result, 0)          sampled by UV from the erosion-res pair
Coverage = saturate(Carve / Full At Depth)
BC.rgb   = lerp(BC.rgb, Exposed Color, Coverage * Color)
RAM.r    = saturate(RAM.r + Roughness * Coverage)
```

A lerp, not the multiply a stain uses. A stain sits on top of what is under it and can only
darken; erosion cuts down to something else, so the authored colour has to be reachable.

`Full At Depth` exists because the carve is in raw height units. Without it every usable carve
saturates instantly and both amounts are all or nothing.

Three implementation notes:

- **Its own shader.** Four more texture slots on `FMixtormatErosionCS` would have to be bound,
  and dummied at the right size, on every carving, blur and resample dispatch with no use for
  them.
- **Through scratch and back.** The pass reads and writes the same two targets, which cannot be
  bound as SRV and UAV at once. The other ping-pong slot is dead at that point and could be
  borrowed, but that is a bet on the slot arithmetic staying as it is, and a wrong bet would only
  show on some layers.
- **No pre-erosion copy.** The carve is still the difference of the two textures the filter has
  been ping-ponging, so the pass samples those by UV rather than keeping a copy of the composited
  height aside before the resample overwrites it.

AO is deliberately not written. Carved channels arguably want to darken, but that is a separate
signal from what the carve *exposes*, and the ridge map already exists as the input a generated
mask would use for it.

## Ridge map

Ridge and drainage fall out of the octave loop at negligible extra cost. Publish the ridge map
as a new weighted signal on the generated
mask node, so drainage lines and creases become mask input without a second evaluation.

This requires the erosion pass to write the ridge map to a target the mask pass can read, which
means one additional full-resolution channel when any erosion effect is present.

## Ordering

Erosion is a post-layer filter. It runs after the owning layer composites, reads the height
and normal that layer just produced, carves the height, and writes both back.

The plan originally specified running it before the layer composited, on the accumulated
height underneath. That was wrong in practice: the layer then paints its own height straight
back over the carve, so the effect only appeared if the layer was hidden. Erosion belongs on
the layer whose height you want carved, and must therefore run after it.

Normals are derived from what was removed. The gradient of `source - carved` becomes a tangent
normal and is combined with the layer normal by RNM, the same construction border lift uses.
Without this the surface displaces but shades flat.

## Limitations

- Gullies are a stylistic approximation, not a physical erosion simulation.
- Tiling constrains scale and lacunarity to values that resolve to whole cells.
- Cost is octaves x gully-field evaluations per pixel per erosion effect; it is not free at 4K.
- The filter assumes a height field with meaningful slope. A flat layer produces nothing, which
  is correct but may read as the effect being broken.

## Validation

1. `Amount = 0` is bit-identical to the unmodified height.
2. A flat height field is returned unchanged.
3. A known ramp gains gullies aligned down the slope.
4. Output tiles with no seam at every supported resolution.
5. Octave count changes detail without shifting large-scale shape.
6. The ridge map is non-uniform for an eroded slope and flat for a flat one.
7. Eroded height drives blend mask, contact AO and border normals together.
8. Baked height matches the preview.
9. Recipes without an erosion effect are unchanged.
10. No third-party licensed source is present in the plugin.

Do not run builds, Unreal, or tests without explicit permission.

## Current state

Built and linking. Never confirmed to produce a visible result in the editor.

Implemented:

```text
EMaterialLabEffectType::Erosion          appended as 2
EMaterialLabEffectClass                  Surface / Filter split
FMaterialLabLayerEffect::ProceduralType  type for effects with no asset
Shaders/Private/MaterialLabGully.ush     hash, periodic noise with derivatives, LIC, soft subtract
Shaders/Private/MaterialLabErosion.usf   carving passes plus a final normal pass
MaterialLabGpuCompositor.cpp             shader class, render data, post-layer dispatch
SMaterialLab.cpp                         Add Child > Effect > Erosion, inspector section
```

Open, in the order worth attacking:

1. Nothing has been seen working. The last report was that erosion only took effect when the
   owning layer was hidden, and that normals did not follow. Both were addressed by the
   post-layer restructure, which has not been tested.
2. Repose scale is unknown for Material Lab height. Set Repose to 0 first: the gate is then
   fully open and carving should be violent and obvious. If 0 still does nothing, the gate is
   not the problem and the next step is a debug view of the height and slope erosion actually
   receives, rather than more parameter guessing.
3. Normal Strength scale is unknown for the same reason. Try well above 1 before concluding
   it does nothing.
4. The ridge map is computed and written but consumed by nothing. It was meant to become a
   weighted signal on the generated mask node.
5. The isotropic mode from the Houdini prototype was never ported. Only the directional LIC
   path exists in HLSL, so the cheap single-pass alternative has never been compared.
6. The erosion child row in the layer stack has a blank name, because the row derives its
   label from the effect asset path and a procedural effect has none.

The Houdini prototype at `Handoff/Houdini/OpenCl/gullyerosion.cl` is the reference for what
the filter should look like, and is the faster place to try changes to the kernel itself.


## The slope blur was the dithering

The guide pre-pass was a fixed nine-tap kernel whose taps sat at exactly +/-BlurRadius texels.
Widening the radius moved the taps apart without adding any, so past one texel a pixel and its
neighbour sampled disjoint sets. At the default radius of 2, pixel x read {x-2, x, x+2} and
pixel x+1 read {x-1, x+1, x+3}, sharing nothing at all -- so the "blurred" guide decomposed into
independent interleaved lattices. A gridded field, not a smoothed one, and raising the slider
made it worse rather than better. That is why no blur amount helped.

Integer radii are the worst case; at fractional ones bilinear pickup gives neighbours partial
overlap. The old comment claimed fractional radii were the point while the default sat exactly
on an integer.

One gridded guide reads strongly because three things downstream depend on it:

- the repose gate multiplies the whole carve, so a gridded slope threshold-dithers everything
- the flow direction steers the gully LIC, so each pixel integrates the noise along a different
  line and the output decorrelates between neighbours
- the Hessian divides by StepUV squared, amplifying guide error by roughly Res^2/R^2 -- only
  live when Cavity Influence is non-zero, which is not the default

And at Slope Blur 0 the guide is the raw source, so both ends of the slider were bad for
different reasons.

The fix: the tap count scales with the radius, not only the spacing. Taps are one texel apart
regardless of radius, so neighbouring pixels always share all but two of them. Separable, one
axis per dispatch, because a 2D kernel this wide is 33x33 at the slider maximum. Costs one extra
erosion-resolution R32F transient and one extra dispatch per pass.

**This changes what Slope Blur does at every non-zero value.** Existing recipes will look
different, and the default of 2.0 may want revisiting now that it means a real two-texel
Gaussian rather than three disjoint samples.

The R32F precision work that preceded this was necessary but not sufficient: it fixed the
storage, while the filter that was supposed to clean the guide up was itself manufacturing the
pattern.

### Still open

The octave floor stops at two pixels per cell (`Cells <= Res/2`), where Perlin gradient noise is
at Nyquist. Deliberately not changed in the same pass as the blur: `Gain` is 0.5, so the finest
octave carries about 1.5% of the base amplitude, and changing two things at once would make a
remaining dither impossible to attribute. Worth a separate look if the blur fix does not fully
land.
