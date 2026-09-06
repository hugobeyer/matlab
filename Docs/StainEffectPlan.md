# Material Lab — Stain Transport

Status: Implemented. Static validation only (dxc on the shader); editor/GPU validation is still
required.

## Goal

Replace the legacy one-pass stain gather with a physical, tileable transport filter that reads
what the layer stack has actually accumulated. One solve exposes two masks:

- **Wet** — mobile water plus absorbed saturation.
- **Deposit** — dirt and minerals dissolved by water and left behind while drying.

Both modes use the same shader and the same state. Selecting a mode only changes which of the two
masks is resolved, so Wet and Deposit instances share one parameter vocabulary.

## Stain is a mask, not a shader

Stain resolves a **layer mask**. It writes no colour, no roughness, no normal and no height.

It used to write two of those. First a `StainColor` multiplied over the composited albedo, then an
absolute roughness target lerped through the stain mask. Both put a post-layer filter in charge of
channels the layer stack had already resolved, and the filter always lost that argument: a tint
strong enough to read also flattened the material underneath it, and a roughness target that
looked right on one surface was wrong on the next layer down. Every channel the filter *didn't*
write was left inconsistent with the two it did.

The mask is the honest primitive. A stain is the *shape* of where liquid ran; what that shape
should look like is a material question, and the layer stack already answers material questions.

```text
rust streak  = a rust material layer, masked by a Deposit stain
wet patch    = a smoother, darker material layer, masked by a Wet stain
dust settle  = a dust layer, masked by a Deposit stain with low Gravity
```

Every one of those composes with height blending, contact AO, border normals, grade and the rest,
because it is an ordinary layer. None of them were expressible when the filter owned two channels
and nothing else.

The practical test Hugo put to it: drop a Fill layer, add a Stain child, and the fill should show
only where the stain ran. That now works, and it works for any layer type.

## Placement: a mask child, not a post-layer filter

Stain runs **inside the layer's child loop**, alongside authored masks, generated masks,
craquelure and colour ID. It reads the mask accumulated so far, resolves its own coverage into it,
and hands the result on as `CombinedMask`.

That is a move from where it used to run. As a Filter it was deferred out of the child loop and
run over the layer's composited output, so it could read that layer's final roughness. As a mask
child it runs *before* the layer composites, so the surface it reads — normal, height, roughness —
is the one accumulated **underneath** the layer.

That is the better read, not a compromise. Liquid runs over the surface that is already there;
`SurfaceFollow` should follow the geometry the layer sits on, and `RoughnessResponse` should drag
on the roughness of what is underneath. It is also exactly the surface the generated mask node
reads (`OutputN`, `OutputRAM`, `HeightTargets` at `1 - (LayerIndex & 1)`), so the two nodes now
agree about what "the surface" means.

`MixtormatEffectClassOf` still reports `Filter` for Stain. That enum answers one question — does
this effect write the effect data target — and the answer is still no. The header comment says so
explicitly rather than leaving the taxonomy to be inferred.

### Mask-chain contract

Stain follows the same rules every other mask node follows:

```text
Initialize     first mask child on the layer; Previous is treated as zero, not as the
               half's white clear
BlendMode      Replace, hardcoded. A run either covers a texel or it does not, and Stain
               exposes no blend control of its own
Strength       the blend weight: saturate(lerp(Previous, Blended, Strength))
SurfaceValid   0 on layer 0, where nothing is composited beneath. The auto source is
               unavailable there, so a stain with no authored source writes
               saturate(Previous) and leaves the chain untouched. A stain with a Liquid
               Mask still runs -- gravity alone drives flow and roughness response goes
               neutral
```

Two consequences worth stating, because both are easy to get backwards:

- The **weight-0 skip** applies only from the second mask child onward. The first child cannot
  skip: it establishes the chain with `Initialize`, and skipping it would leave the mask half at
  its white clear — the layer fully visible, the opposite of "no stain".
- `UseLayerMask` is `MaskPassIndex > 0` for the same reason. On the first child the read half is
  cleared to **white**, so trusting it would source liquid over the entire surface instead of
  falling back to curvature.

## Source model

Liquid can come from three places, and none of them is required:

- the stain's own **Liquid Mask** (an asset or a texture, tiled and optionally inverted);
- the **mask accumulated by preceding children**, when Liquid Mask is unset;
- the **auto source** — four weights over the surface accumulated below the layer.

A stain with no Liquid Mask and no preceding mask child is driven entirely by the auto source.
A stain with only a Liquid Mask and no mask child in front of it works too; both were broken
before (see *The two ways this used to be unusable*).

### Auto source

`EvaluateSurfaceSource` reads the surface accumulated *below* the layer and weighs four signals
into a source field. The vocabulary deliberately mirrors the generated mask node, because both
nodes are asking the same question of the same textures.

```text
Concavity   cavities collect liquid                       curvature from the normal
Convexity   exposed detail sheds it, and is where a       curvature from the normal
            runoff commonly starts
Occlusion   sheltered geometry both collects and keeps    accumulated AO (RAM.g)
            liquid
Height      signed: positive sources runoff from a        accumulated height, plus a bias
            crest, negative pools it in the recesses
Slope       faces tilted into the flow catch liquid,      normal, against the flow direction
            faces tilted away shed it
```

Two of those need distinguishing from things that already existed:

- **Occlusion is not Concavity.** Curvature is measured here from the normal alone. AO is what the
  stack below actually resolved, contact AO between layers included, so it sees shelter that no
  amount of normal analysis would show.
- **Slope is not Surface Follow.** Surface Follow decides where liquid *goes*; Slope decides where
  it *lands*. It is the term that makes an upward-facing sill wet with no mask at all.

The four signals are **summed**: a cavity that is also occluded and also low should collect more
liquid than any of those alone. Concavity and Convexity default to 0.35 and 0.15; Occlusion,
Height and Slope default to zero, so curvature alone remains the starting behaviour and the other
three are opt-in.

Curvature evaluation is skipped entirely when both curvature weights are zero, and each other
signal is skipped when its weight is zero.

Slope uses the normal's XY at its own magnitude rather than normalized, so a steeper face catches
more. The cost is that the response scales with how strong the normals underneath are — raising a
lower layer's Normal Intensity also raises this term. Normalizing would trade that for a slope
response identical on a 5-degree ledge and a 60-degree wall, which is worse.

### How the two sources combine

The auto source and the authored source **multiply**, in `LiquidSource`, and each half falls back
to 1 when it is not in use:

```text
mask only        confines liquid to the mask
auto only        places liquid by geometry
both             restricts the geometric source to where the mask allows it
neither          nothing asks for liquid; the source field is zero
```

They used to combine with `max`, which meant a mask could not actually gate anything: with the
default Concavity of 0.35, a stain with a carefully painted Liquid Mask still sourced liquid at
0.35 everywhere the mask was black. That reads as the mask being ignored, and it was the most
common way to conclude the effect was broken.

Both the initial injection and the per-iteration top-up use the same combined field. The top-up
used to read the authored mask alone, which left an auto-sourced stain with one injection at init
and no top-up at all — so auto mode read as far weaker than mask mode, for reasons that had
nothing to do with either.

Dirt can come from a separate Dirt Mask; unset it reuses the liquid source. Dirt is carried only
by water, so an auto-sourced liquid and an authored dirt map still combine naturally.

Optional authored masks are sampled directly at their native size. `AddCopyTexturePass` is not a
resampler, and copying an arbitrary authored size into the composition size is invalid on D3D12.

## The two ways this used to be unusable

Both were the same root cause wearing two faces: a Stain child did not count as a mask.

**A stain alone did nothing.** `bHasMask` is gathered on the game thread from the layer's
children, and only Mask, Generated, Craquelure and ColorId children set it. A Stain child did not,
so the composite ran with `HasMask = 0`, ignored the mask chain the stain had just resolved into,
and the layer covered fully. Adding any other mask child in front of the stain set the flag and
made the stain appear to start working — which is the workaround the bug forced, not a design.

**A stain's own Liquid Mask needed a mask child in front of it.** Same cause. The Liquid Mask was
being read correctly the whole time; the resolved coverage simply went into a chain the composite
had been told to ignore.

The gather now sets `Data.bHasMask = true` for a Stain child, for the same reason every other mask
node does: it resolves into the chain, so it makes the layer masked.

Two consequences of that one line, both intended:

- **`HeightSource` follows.** In `Automatic` mode the flag picks `CombinedMask` over `Constant` for
  a layer with no packed height, so a stain-masked Fill layer takes the run's shape as its height
  and participates in height blending, contact AO and border normals. That is what every other
  mask child already gives such a layer. A layer backed by a surface asset keeps its packed height
  and is unaffected.
- **A stain that cannot run must be the identity, not a zero mask.** With the flag set, the
  composite now honours the chain, so a stain that bails has to leave something usable in it. The
  resolve's early-out writes `1.0` when it is the first mask child and `saturate(Previous)`
  otherwise — a fully visible layer, or the chain unchanged. Writing `Previous` in both cases
  would hide the layer completely on the bottom layer, because `Initialize` forces `Previous` to
  zero there.

`Strength` at 0 is *not* special-cased: as the only mask child it resolves to an empty mask and
hides the layer, exactly as `Weight` 0 does on a generated mask. That is the mask system's
existing contract, not a stain quirk.

## Solver

The state uses two half-float RGBA textures:

```text
State A = water, saturation, dissolved dirt, deposit
State B = velocity.xy, flow direction as an angle, source field
```

The flow direction is stored as an angle rather than as a vector so the fourth channel can carry
the combined source field. The per-iteration top-up needs the same source the initial injection
used, and re-deriving it would mean running the curvature analysis on every step instead of once.
Half precision on an angle in radians is ~0.001 rad, which is far below what a direction needs.

The shader has three modes:

```text
Initialize  auto source + liquid mask, surface flow, initial velocity
Simulate    pressure, advection, spread, absorption, drying, dissolve, deposit
Resolve     wet/deposit mask into the accumulated layer mask, optional debug view
```

State remains half precision because it is bounded transport data and is never differentiated like
erosion height.

## Material response comes from the layers below

One control — **Surface Response** — decides how much of the solve comes from the material
accumulated beneath the layer. It drives two couplings, because a rough surface genuinely does two
things: it drags a run, and it drinks.

```hlsl
const float Response = saturate(SurfaceResponse);

// Drag, wander, lateral dispersion.
const float LocalRoughness = lerp(0.5f, CompositeRoughness, Response);

// Absorption capacity and rate.
const float SurfacePorosity = saturate(CompositeRoughness * (1.0f - Metallic));
const float LocalPorosity   = lerp(0.5f, SurfacePorosity, Response);
```

`0` ignores the surface and solves against a neutral middle; `1` takes it directly. The RAM is
sampled once and answers both questions, because both are properties of the material the liquid is
on rather than settings on the liquid.

### Why this is one control and not two

It was two — `Roughness` and `Porosity` — and they were redundant twice over.

- **Same input.** Both read `SourceRAM.r`. `SurfacePorosity` differs only by the metallic term, so
  on any dielectric the two numbers were identical. Two sliders, one signal.
- **Porosity duplicated Absorption.** `AbsorptionRate` is
  `(0.002 + Absorption * 0.055) * LocalPorosity`, so `Absorption` was already the control for how
  much a given material takes up. A glazed brick — rough, but sealed — is full Surface Response
  with `Absorption` at zero, and that always worked.

The metallic term is what keeps `LocalPorosity` from being a copy of `LocalRoughness`, and it is
the right shape: either term at its extreme shuts absorption off, which is what both materials
actually do. Metal still drags a run normally — only the drinking stops.

### What porosity actually does

It appears in exactly two places, both in the absorption step, and nothing else reads it:

```hlsl
const float Capacity = 0.55f + 0.65f * LocalPorosity;              // 0.55 .. 1.20
const float AbsorptionRate = (0.002f + Absorption * 0.055f)
                           * LocalPorosity
                           * (1.0f - Saturation01);
const float Absorbed = min(Water, min(AbsorptionRate, Capacity - Saturation));
Water -= Absorbed;
```

Three consequences follow:

- **Wet mask strength.** The Wet mask is built from saturation, so porosity is effectively its
  gain. At low porosity the mask comes almost entirely from the thin surface-water term.
- **Run length, inversely.** Absorbed liquid leaves `Water`, so it can no longer advect. High
  porosity drains the flow into the surface and gives short soaked patches; low porosity keeps it
  mobile and gives long runs.
- **Deposit, indirectly.** Dissolution reads `Water`, never porosity — but absorption removes
  water, so a thirstier surface carries less dirt.

Roughness, by contrast, touches only `Inertia`, `Wander` and `SpreadAmount`: how the run *moves*,
never how much liquid leaves the surface. That is the real distinction between the two couplings,
and it survives the merge intact.

## Mask normalization

Both masks normalize against what the solve actually put into the surface, not against an absolute
physical scale. This is the difference between shading and masking, and getting it wrong made the
whole effect look broken in a way that pointed at the wrong culprit.

The resolve used to divide saturation by `Capacity` and run the deposit through
`1 - exp(-Deposit * 2.5)`. Both are honest physical ratios. At the default parameters they resolve
to roughly **0.18** and **0.004**:

```text
Wet      saturation tops out near 0.21 against a Capacity of 1.20        -> ~0.18
Deposit  deposit accumulates to ~0.0017; 1 - exp(-0.0017 * 2.5)          -> ~0.004
```

As a roughness lerp those were a visible sheen and a faint residue — fine. As a **mask** they mean
the layer is 18% visible, or not visible at all. A Fill layer with metallic and zero roughness,
masked by a stain, therefore showed almost none of either, and the surface below showed through.
That reads as the stain overriding roughness, metallic and F0 — channels it does not write, and no
longer even binds.

Nothing was overriding anything. `Alpha = PlacementMask` with default layer settings, so every
channel was being applied at exactly the mask's value, and the mask's value was near zero.

A mask has to reach 1 where the effect is fully present. The reference is the liquid the solve
delivers:

```hlsl
// One injection at initialization plus the per-iteration top-up.
const float Injected = max(SourceAmount, 0.0f) * (1.0f + (float)Iteration * 0.06f);

// Half the injected volume, because evaporation and runoff mean a texel never
// absorbs everything that passes over it.
const float WetMask = saturate((State.y + State.x * 0.20f) / max(Injected * 0.5f, 1.0e-4f));

// Dirt that settled, against the dirt that much liquid could dissolve.
const float LocalDirt = saturate(DirtAmount);
const float Carried = Injected * LocalDirt * (0.004f + LocalDirt * 0.018f);
const float DepositMask = saturate(State.w / max(Carried * 0.25f, 1.0e-6f));
```

Two properties this buys:

- **Scale-free.** `Liquid Amount` now controls where liquid *reaches* rather than how *visible* the
  result is, which is what an amount on a source ought to mean. Raising it no longer doubles as a
  brightness control on the mask.
- **The references track their own parameters.** The deposit reference reuses the dissolve rate
  expression from `SimulateState` rather than restating a number, so moving `Dirt Amount` moves
  both sides together instead of pulling them apart.

Porosity still drives absorption inside the solve, so a less porous surface still produces a weaker
wet mask — it just no longer sets the normalization as well, which was double-counting it.

The two divisors (`0.5` and `0.25`) are the only tuned constants here. If Wet saturates too
readily, raise the first; if Deposit does, raise the second.

## Resolution and step length

The solve runs at composition resolution. There is no divisor.

The reduced-resolution solve was cheap and it looked it. Transport state advected on a quarter-side
grid and then resampled up softened every run edge, so the thing the filter exists to produce —
a run with a legible boundary — was the first casualty of the optimisation. Detail below the
simulation texel could not be transported at all, which is exactly the detail a stain follows.
It matters more now than it did: the output is a *mask*, and a soft mask boundary is visible on
every channel of the layer it gates rather than on a roughness lerp alone.

Full resolution costs what it costs: twenty steps plus a resolve is twenty-one full-resolution
dispatches, where the divisor-4 path cost roughly 2.3. To buy some of that back in reach rather
than in fidelity, advection steps `StainStepScale` texels per iteration instead of one:

```hlsl
static const float StainStepScale = 2.0f;
const float2 BackUV = frac(UV - Velocity * SimTexel * StainStepScale);
```

Only the transport offset is scaled. The water-gradient taps and the lateral spread taps stay at
one texel, because those estimate derivatives of the field rather than travel through it; scaling
them too would change the diffusion character and read as a bug later.

Three consequences, none of them subtle enough to leave undocumented:

- **Runs are shorter at a given iteration count.** A step now travels two full-resolution texels;
  the old divisor-4 solve travelled one simulation texel, which was four. Iteration counts carried
  over from the old solve produce roughly half the length, at much higher fidelity. The
  `Iterations` default stays at 20 — halving it as well would quarter the run and read as broken.
  Raise it if an existing recipe wants its old length back.
- **Transport rates are unchanged and deliberately so.** `EvaporationRate`, `AbsorptionRate` and
  `DepositionRate` are still per iteration, so a parcel of water now covers twice the ground per
  unit of drying. Wet reads longer and thinner; deposits land further downstream. That is the
  displacement change working as asked, not drift — scaling the rates by `StainStepScale` would
  undo the reach the larger step was added to buy.
- **Sub-step continuity is the price.** `MaxSpeed` is `1.25 + |Gravity| * 1.75`, so at the clamp a
  step backtraces up to six texels through a single bilinear tap and skips five of them. The step
  scale buys reach at the cost of continuity within a step; if runs read as dashed rather than
  continuous, that is the term to lower, not `Iterations`.

## Debug view

The Stain group in the inspector carries the same feature-preview eye the layer's Feature,
Contact AO, Border Normal and Generated Mask groups carry — `MakeFeaturePreviewButton`, the
layer-stack eye widget, sized from `MixtormatTokens::LayerEyeSize`. No bespoke button, no local
styling.

Stain cannot use the composite's debug write, because by the time the composite runs the mask has
already been folded in. `FMixtormatStainCS` therefore takes the debug UAV itself, and the resolve
writes the same unlit ramp every other preview writes, over the **raw wet or deposit mask** —
before `Strength` blends it into the chain, which is what the other previews show too. The write
is gated on the selected layer the way the composite gates its own, so two stains on different
layers cannot fight over one target. Only the resolve binds the shared debug target; the solve
passes take a dummy.

Two things the move into the child loop made necessary, both of which showed up as "the eye does
nothing":

- **The composite must skip `WriteDebug` when Stain is selected.** Every other preview mode is a
  signal the composite derives, so the composite writes it. Stain now resolves *before* that pass,
  and the composite has no case for mode 6 — it falls through to `DebugValue` 0 and paints flat
  `DebugLow` straight over the view the stain just wrote. Excluding the mode is load-bearing, not
  an optimisation.
- **A stain that bails still writes the preview.** The resolve's early-out returns before the
  normal write, so it writes an empty ramp on the way out. An empty view is the honest answer —
  there is genuinely no coverage — and it is a different answer from leaving the target untouched,
  which shows whatever the last composite left behind and reads as a broken eye.

A muted stain (`Strength` or `Liquid Amount` at zero, past the first mask child) is skipped
entirely and writes nothing, which leaves the target at its clear — `DebugLow`, i.e. zero
coverage. That is the same answer by a different route.

The generic Layer Mask preview also snapshots the chain after a stain child, so the accumulated
mask can be inspected the same way it can after any other mask node.

## Tiling

Every UV sample uses a wrap sampler. Advection wraps with `frac`, authored mask tiling is integral,
and the deterministic source breakup hashes the periodic simulation texel domain.

## Compatibility

The generated `MLFX_Stain` asset is no longer imported or listed. Existing recipes that reference
it still resolve as Stain and run through the transport solve, so serialized stacks do not lose
their child.

Deserialize-only, in `FMixtormatLayerEffect`:

```text
StainColor, StainColorAmount    the effect writes no base colour
StainRoughness                  the effect writes no roughness
StainSolveDivisor               the solve is full resolution
StainPorosity                   merged into StainSurfaceResponse; Absorption controls uptake
StainRoughnessResponse          renamed StainSurfaceResponse, which drives absorption too
StainHeightInfluence, StainHeightWarp, StainHeightBias, StainHeightContrast
StainFlowAmount, StainFlowRadius, StainFlowSmoothing
```

Deserialize-only, on `UMixtormatEffect`:

```text
DefaultStainColor, DefaultStainRoughness
DefaultStainHeightInfluence, DefaultStainHeightWarp
DefaultStainHeightBias, DefaultStainHeightContrast
```

Those six were still `EditAnywhere` under a "Stain Defaults" category after the solve stopped
reading them, which is worse than dead code: a person could open `MLFX_Stain`, tune them, save,
and get nothing. They are deprecated and no longer editable. The fields stay so the existing asset
loads without dropping them.

Nothing in the plugin reads any deprecated stain field — neither set has a single reference in a
`.cpp`. They exist to round-trip old recipes and assets, and deleting them would silently drop
that data on the next save.

Existing recipes will look different, and there is no way around that: a stain that used to tint
and gloss now masks nothing, because the child it lives under has no layer to reveal. The fix per
recipe is to give the stain a layer to mask — the material the stain was approximating with a
tint — which is the thing the old model could not express.

## Performance follow-ups

Full resolution is the right default for quality, and it is roughly nine times the solve cost of
the divisor path. None of the following is implemented; listed so the next pass has a starting
point rather than a rediscovery.

- Cache solver state keyed on the source textures and transport parameters, so a composite that
  changed nothing upstream of the stain reuses the solve.
- Make `Wet`/`Deposit` rerun only the resolve. The two masks already come out of one state, so
  switching output should never re-solve.
- Share one solve between a Wet and a Deposit child whose transport parameters are identical.
- Build conservative active tiles from the source masks plus the reach implied by gravity, step
  scale and iteration count, and dispatch only those tiles indirectly. Inactive tiles stay exact —
  skipped, never approximated or downsampled.
- Skip dirt work when `DirtAmount` is zero. (Per-signal skipping in the auto source is done:
  curvature, occlusion, height and slope each cost nothing at weight zero.)

The first three are cheap and independent of the RDG graph shape. Sparse tiles are the largest win
and the largest change.
