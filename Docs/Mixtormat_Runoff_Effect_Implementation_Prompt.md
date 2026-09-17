# Mixtormat — Runoff Effect Implementation Prompt

Implement a new Mixtormat effect called **Runoff**.

Repository:
- `https://github.com/hugobeyer/matlab`

The effect should be a **cheap procedural alternative to the current iterative Stain effect**, focused on vertical runoff, layered deposits, rust/water streaks, mineral buildup, dirt below ledges, and similar material behavior.

Do **not** replace or modify the existing Stain effect beyond any shared utility work that is clearly appropriate. Runoff is a separate effect.

---

## Core behavior

The effect should conceptually do:

```text
height + incoming mask
        ↓
cavity / ledge detection
        ↓
source = incoming mask × surface response
        ↓
optimized built-in low-element-size fractal mask noise
        ↓
directional Gaussian runoff / streaking
        ↓
automatic layered strata
        ↓
per-strata vertical-only warp
        ↓
terminal lip / sediment buildup
        ↓
final Runoff mask
```

The result should feel like overlapping runoff layers rather than a single blurred streak.

Reference look:
- broad soft dirty runs
- thinner longer streaks
- uneven layered deposits
- different runoff lengths from the same source
- slightly different vertical shape per stratum
- visible terminal buildup / lip on some layers
- no arbitrary sideways flow

---

## Inputs

Use the same conventions as other Mixtormat effects.

Primary inputs:
- `Height`
- incoming/scoped `Mask`

The source should be derived from both:

```cpp
Source = IncomingMask * SurfaceResponse;
```

Where `SurfaceResponse` is based on cavity/concavity and ledge/downhill edge detection from height.

The incoming mask is important:
- mask intensity should affect how strongly and how far areas streak
- stronger mask values can produce longer runoff
- weak values should produce shorter/subtler runoff

---

## Exposed artist controls

Keep the UI simple.

Use approximately these controls/ranges:

```text
Gravity Angle
  range: -180 .. 180
  default: -90

Streak Radius
  range: 8 .. 512
  default: 320

Streak Softness
  range: 0.05 .. 1.0
  default: 0.46

Surface Influence
  range: 0 .. 1
  default: 0.95

Strata Amount
  range: 0 .. 1
  default: 0.75

Warp Scale
  range: 1 .. 64
  default: 18

Warp Amount
  range: 0 .. 2
  default: 1.50

Lip Strength
  range: 0 .. 1
  default: 0.55

Strength
  range: 0 .. 1
  default: 0.25

Seed
  range: 0 .. 9999
  default: 1
```

Do not expose low-level implementation parameters unless absolutely necessary.

---

## Automate internally

These should be derived automatically rather than exposed:

```text
cavity strength
ledge strength
strata count
strata spacing
strata length variation
strata opacity falloff
warp contrast/remap shaping
lip width
accumulation normalization
internal noise roughness
internal noise lacunarity
internal noise octave count
internal noise element size
```

Suggested behavior:

```cpp
CavityStrength = 35.0f;
LedgeStrength  = 22.0f;
```

Automatic strata count can scale with streak radius, e.g.:

```cpp
int32 StrataCount = 2;
if (Radius >= 160) ++StrataCount;
if (Radius >= 320) ++StrataCount;
if (Radius >= 480) ++StrataCount;
StrataCount = FMath::Clamp(StrataCount, 2, 5);
```

`Strata Amount` should control the visual separation/visibility of strata rather than exposing count, spacing, falloff, etc. separately.

Normalize overall accumulation against the generated strata count so increasing strata does not simply brighten the whole mask.

---

# Directional Gaussian runoff

Do not implement this as a long iterative simulation.

Use a **directional Gaussian blur/streak** along `Gravity Angle`.

Conceptually:

```cpp
float Reach = SourceMaskIntensity * Radius;
float Sigma = Reach * Softness;
```

Then sample only along the gravity axis.

This should behave like a one-sided / directional Gaussian runoff rather than a symmetrical blur.

Important:
- source mask intensity controls reach
- use Gaussian weighting
- derive tap count internally from sigma/radius
- do not expose `Samples`
- clamp internal tap count to a safe maximum
- preserve tiling

For performance, consider paired/bilinear Gaussian sampling if the shader architecture supports it cleanly.

---

# Strata

Strata means **multiple layered runoff interpretations of the same source**, not horizontal banding and not arbitrary procedural stripes.

Each stratum should automatically vary:

```text
start offset
effective length
opacity
vertical warp
terminal lip
```

For example:

```cpp
float LayerT = LayerIndex / max(StrataCount - 1, 1);

float StartOffset = AutoSpacing * LayerIndex * Radius;
float LengthMul   = 0.85f + LayerT * AutoLengthStep;
float Opacity     = 1.0f / (1.0f + LayerIndex * AutoFalloff);
```

The layers should overlap and produce a natural stacked runoff look.

---

# Vertical-only warp

Warp each stratum differently, but **only along the gravity axis**.

Do not displace sideways.

The behavior should resemble the SideFX workflow where a procedural scalar field is:
1. generated
2. remapped / contrast-shaped
3. combined
4. used as a displacement amount

Conceptually:

```cpp
float WarpMask = max(RemappedNoiseA, ContrastedNoiseB);
float WarpSigned = WarpMask * 2.0f - 1.0f;

float WarpedDistance =
    BaseDistance +
    WarpSigned *
    WarpAmount *
    Radius *
    LayerMultiplier;
```

Different strata should receive different deterministic offsets/seeds/scales so they do not all deform identically.

---

# Built-in default mask noise

Runoff should have an internal default breakup noise with a **small element size**, inspired by the supplied SideFX noise implementation.

Do not port the full SideFX generic system.

The supplied reference supports:
- many noise types
- arbitrary dimensionality
- animation/time
- loops
- post processing
- optional source inputs
- arbitrary channel counts
- several fractal modes
- high octave counts

Runoff needs only a tiny specialized subset.

Use approximately:

```text
Noise type:
  tileable 2D Perlin / gradient / value-style noise

Element size:
  ~0.03–0.05

Octaves:
  3

Roughness:
  ~0.5

Lacunarity:
  ~2.0

Mode:
  mild sharp/ridged character if useful

Seed:
  reuse Runoff Seed
```

The SideFX reference defaults to:

```c
#bind parm featuresize float2 val=0.1
#bind parm fractaltype int val=1
#bind parm roughness float val=0.5
#bind parm octaves float val=8
#bind parm lacunarity float val=2.1
```

For Runoff:
- use smaller element size
- reduce octaves heavily
- keep it tileable
- specialize to scalar 2D output
- remove all animation/3D/channel/post-processing/general-noise machinery

The reference's fractal loop repeatedly applies roughness/lacunarity weighting across octaves. Preserve the useful visual idea, but specialize it:

```c
base = noise(p);
weight = 1;

for each octave:
{
    weight *= roughness;
    p *= lacunarity;
    base += noise(p) * weight;
}
```

Three octaves should be enough.

---

## Very important performance rule

Evaluate the built-in fractal noise **once per output pixel** or at most a very small fixed number of times per output pixel.

Do **not** evaluate FBM:
- inside every Gaussian tap
- inside every stratum × Gaussian tap
- repeatedly for values that are invariant across the blur

Prefer:

```cpp
float BaseNoise = RunoffFBM(UV, Seed);
```

Then derive cheap per-strata variants from it, or at most compute one additional decorrelated low-cost noise value per stratum.

Possible cheap derivation:

```cpp
float N0 = BaseNoise;
float N1 = Frac(BaseNoise * 1.73f + 0.31f);
float N2 = Frac(BaseNoise * 2.17f + 0.57f);
```

If this produces visible correlation, use one extra lightweight noise lookup rather than a new full FBM.

---

# Default mask breakup

The internal noise should subtly break up the surface source before runoff.

Conceptually:

```cpp
float Noise = RunoffFBM(UV, Seed);

float Breakup =
    SmoothStep(0.28f, 0.72f, Noise);

float Source =
    IncomingMask *
    SurfaceResponse *
    Lerp(1.0f, Breakup, 0.45f);
```

The effect should already look interesting with default settings.

Do not add separate public controls for:
- Noise Scale
- Noise Octaves
- Noise Roughness
- Noise Lacunarity
- Noise Contrast

Reuse `Warp Scale`, `Warp Amount`, `Seed`, and automated internal constants where sensible.

---

# Lip / sediment buildup

Each stratum should be able to generate a narrow terminal deposit near its effective endpoint.

Conceptually:

```cpp
float NormalizedDistance = WarpedDistance / max(Reach, Epsilon);

float Lip =
    1.0f -
    smoothstep(
        0.0f,
        AutoLipWidth,
        abs(NormalizedDistance - 1.0f));
```

Combine terminal deposits across strata.

`Lip Strength` remains exposed.

Lip width should be automated from streak softness and/or radius.

---

# Surface source

Use height to derive the initial surface response.

The prototype used approximately:

```cpp
float Cavity =
    saturate((AverageNeighborHeight - Height) * 35.0f);

float Ledge =
    saturate((Height - HeightAlongGravity) * 22.0f);

float SurfaceResponse =
    saturate((Cavity + Ledge) * SurfaceInfluence);

float Source =
    IncomingMask * SurfaceResponse;
```

Adapt this to Mixtormat's existing height conventions and coordinate system.

Do not introduce expensive large-neighborhood curvature work if a compact local approximation is sufficient.

---

# Tiling

The effect must remain tileable.

All:
- procedural noise
- directional sampling
- strata offsets
- Gaussian samples

must respect wrapped texture coordinates / Mixtormat tiling behavior.

Avoid seams at UV borders.

---

# Integration requirements

Implement Runoff following the existing Mixtormat effect architecture.

Include:
- runtime effect struct/settings
- serialization/defaults
- Slate inspector controls
- tooltips
- compositor integration
- shader parameter setup
- HLSL/USF shader
- scoped mask behavior
- layer-local behavior
- preview support
- bake support
- undo/redo
- duplication/copy behavior
- any versioning needed
- tests

Follow existing naming/style conventions.

Do not invent a parallel effect framework.

---

# Scope behavior

Runoff must respect the same local/scoped mask semantics as other Mixtormat effects.

Verify:
- layer-local mask
- scoped mask
- effect opacity
- disabled effect
- hidden layer
- nested effect chains
- interaction with Flow Warp / Blur / Chipping where applicable

Do not let Runoff leak beyond its intended placement mask.

---

# Performance goals

Runoff is specifically intended to be a cheaper alternative to iterative Stain for this class of effect.

Avoid:
- iterative simulation
- full-resolution ping-pong state
- FBM in Gaussian loops
- redundant source reconstruction inside every tap if it can be precomputed
- unnecessary intermediate surface writes
- unnecessary full-resolution copies

Prefer:
- single/few compute passes
- compact scalar intermediate fields where useful
- reusable source/noise fields
- directional Gaussian filtering
- low octave count
- one final mask resolve

If a preparation pass for `Source + Noise` significantly reduces repeated work in the Gaussian stage, use it.

Profile before deciding whether one giant shader or a small two-pass pipeline is cheaper.

A likely efficient architecture is:

```text
Pass 1:
  Height + Mask
  -> Surface Response
  -> Built-in Noise
  -> Source Field

Pass 2:
  Source Field
  -> Directional Gaussian
  -> Strata
  -> Vertical Warp
  -> Lip
  -> Final Runoff Mask
```

If one pass is genuinely cheaper without duplicating expensive work, keep it one pass.

---

# SideFX reference code

Use the supplied Houdini/OpenCL noise implementation as a **reference only**.

Important pieces from it:

### Default fractal setup

```c
#bind parm featuresize float2 val=0.1
#bind parm fractaltype int val=1
#bind parm roughness float val=0.5
#bind parm octaves float val=8
#bind parm lacunarity float val=2.1
```

### Perlin base evaluation

```c
static float
_fbm_noisewrap_perlin2_1(float2 pos, int2 period, float c)
{
    float v = mx_perlin_noise_float_2(pos, period) * 0.5f;
    return _apply_contrast_float(v, c);
}
```

### Fractal accumulation concept

```c
RTYPE base = 0;
base = CALLFUNC(NOISENAME, PDIM, RDIM);

float gain = roughness * min(lacunarity, 1.0F);

do
{
    weight *= gain;
    oct += 1;

    p *= lacunarity;
    period *= (int)lacunarity;
    p -= octaveoff;

    RTYPE adjust = 0;
    adjust = CALLFUNC(NOISENAME, PDIM, RDIM);

    base += adjust * weight;
} while (oct < octaves);
```

### Feature-size coordinate scaling

```c
p -= @off;
p /= @featuresize;
```

That is the main behavior to preserve conceptually:
- feature size controls coordinate frequency
- roughness controls octave amplitude
- lacunarity increases octave frequency
- multiple octaves add fractal detail

Do not carry over the full generic implementation.

---

# Existing OpenCL Runoff prototype reference

The current Houdini prototype uses the following key logic.

### Source

```c
float cavity =
    clamp(
        (avgH - hC) * cavity_strength,
        0.0f,
        1.0f);

float ledge =
    clamp(
        (hC - hDown) * ledge_strength,
        0.0f,
        1.0f);

float sourceC =
    maskC *
    clamp(
        cavity * cavity_amount +
        ledge  * ledge_amount,
        0.0f,
        1.0f);
```

### Automatic strata

```c
int count = 2;
if (radius >= 160) count++;
if (radius >= 320) count++;
if (radius >= 480) count++;
count = clamp(count, 2, 5);
```

### Automatic internal values

```c
float strata_spacing =
    lerp(0.55f, 1.15f, strataAmount);

float strata_length_step =
    0.08f + 0.04f * strataAmount;

float strata_opacity_falloff =
    lerp(1.15f, 0.75f, strataAmount);

float warp_contrast =
    clamp(
        0.35f - warpAmount * 0.10f,
        0.12f,
        0.35f);

float lip_width =
    0.35f +
    streakSoftness * 0.35f;
```

### Gaussian

```c
float maxReach =
    radius * lengthMul;

float sigma =
    max(
        maxReach * streakSoftness,
        0.75f);

int taps =
    ceil(3.0f * sigma);
```

### Mask-controlled reach

```c
float reach =
    maskValue *
    maxReach;
```

### Gaussian weight

```c
float x =
    localDistance /
    sigma;

float gaussian =
    exp(-0.5f * x * x);
```

### Vertical-only warp

```c
float warpSigned =
    warpMask * 2.0f - 1.0f;

float warpPx =
    warpSigned *
    warpAmount *
    radius *
    LayerMultiplier;

float warpedDistance =
    max(
        localDistance + warpPx,
        0.0f);
```

### Lip

```c
float normalizedDistance =
    warpedDistance /
    max(reach, 1e-5f);

float edge =
    1.0f -
    smoothstep(
        0.0f,
        lipWidth,
        abs(normalizedDistance - 1.0f));
```

These references describe the intended behavior, not mandatory literal implementation.

---

# Tests

Add tests covering at least:

```text
neutral / zero strength
zero mask
full mask
gravity direction
mask intensity changes streak reach
height cavity/ledge source
strata amount
warp amount
seed determinism
tiling
lip generation
scoped mask confinement
different resolutions
preview vs bake consistency
serialization/default values
```

Also compare representative output at:
- 1K
- 2K
- 4K

The apparent physical scale of the effect should remain stable.

---

# Deliverables

Return:

1. full list of changed files
2. full implementation
3. explanation of the final GPU pipeline
4. public parameter list/ranges/defaults
5. what was automated internally
6. performance rationale
7. shader/pass count
8. build result
9. relevant test results
10. any remaining performance concerns

Do not make unrelated changes.

The priority is:

**artist-friendly defaults + procedural quality + substantially cheaper execution than iterative Stain.**
