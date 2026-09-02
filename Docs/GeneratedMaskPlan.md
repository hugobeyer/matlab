# Material Lab — Generated Mask Plan

Status: Not implemented. Design only.

## Goal

Add a third ordered layer-child type that produces a mask from the accumulated surface
below the owning layer instead of from a texture. This is the placement input for dust,
snow, grunge, dirt, and wear: signals that belong in crevices, on up-facing slopes, or in
occluded areas, and that must follow whatever material was composed underneath.

A Generated Mask child is a single node that mixes weighted surface signals. Several such
children may be stacked and combined with texture masks through the existing ordered mask
accumulator and its blend modes.

## Scope

This is editor-time texture compositing for tiling surfaces: dust in brick mortar, grunge in
crevices, wear on raised detail. Every signal is derived in tangent space from the accumulated
maps below the layer.

World-space placement — snow driven by world up, projected dirt, per-prop variation — is not
part of this feature and must not be added to it. A user who wants that applies it downstream
in their own material or through the later prop workflow, using the baked outputs this plan
produces as input.

## What already exists

`MaterialLabComposite.usf` already derives every input signal except direction:

```text
Cavity / Convex / Curvature   MaterialLabEvaluateCurvature(PreviousN, ...)
Inverted AO                   PreviousPacked.g with InvertAOFeature
Height                        PreviousHeightValue with InvertHeightFeature
```

They are exposed only as three per-layer scalars — `FeatureInfluence`, `AOFeatureInfluence`,
`HeightFeatureInfluence` — that default to `0.0`, are multiplied together into one fixed
`SurfaceMaskWeight`, and sit outside the ordered mask stack. There is one combination per
layer, no ordering, no blend modes, and no per-signal shaping.

This plan promotes those signals into the child stack. It does not replace them.

## User experience

- `Add Child` gains `Generated Mask` beside `Mask` and `Effect → Peeling`.
- The child row uses the existing hierarchy rail, drag reordering, and overflow menu.
- Its thumbnail is a generated preview of the node's own output, not an asset thumbnail.
- Selecting it opens a Generated Mask inspector below Layer Settings.
- The existing debug eye previews the node's output in isolation.
- Blend Mode, Weight, Invert, Balance, Contrast, and Offset behave exactly as for a texture
  mask, because the node writes into the same accumulator.

## Data contract

Append-only child type:

```text
EMaterialLabLayerChildType
    Mask       = 0
    Effect     = 1
    Generated  = 2
```

`FMaterialLabLayerChild` gains `FMaterialLabGeneratedMask Generated`.

```text
Signal weights          signed, default 0
- CurvatureWeight
- CurvatureBias         0 = cavity, 1 = convex; default 0
- DirectionWeight
- DirectionAngle        degrees in tangent space; default 90 (+Y)
- DirectionBroadness    falloff exponent
- AOWeight              positive weight uses inverted AO
- HeightWeight
- HeightBias            signed

Shaping
- bNormalizeWeights     default true
- Broadness             curvature sample radius, 1-32
- Bias                  MaterialLabBias01 on the mixed result
- WarpAmount            default 0
- WarpSource            0 normal slope, 1 height gradient
- WarpRadius            gradient sample distance in pixels

Accumulator controls (identical to FMaterialLabMaskLayer)
- bEnabled, BlendMode, Weight, bInvert, Balance, Contrast, Offset
```

Every weight defaults to `0`, so an added node is neutral until the user assigns intent.

## Evaluation

All signals read the accumulated state **below the owning layer** — the same inputs the
per-layer curvature path uses today. They never read intermediate state produced by earlier
children of the same layer.

### Direction

Tangent-space slope direction. Flat is neutral; a face tilted toward the chosen angle reads high.

```text
D    = float2(cos(DirectionAngle), sin(DirectionAngle))
Slope = dot(NormalXY, D)
Direction = pow(saturate(Slope), max(DirectionBroadness, 0.001))
```

Y is tangent-space up by design. Over a ridge this lights the flank tilted toward the
chosen angle and leaves the opposite flank at zero, which is the intended one-sided
accumulation. Rotating `DirectionAngle` selects which flank receives it.

### Warp

Slope flow. Settling material runs along the surface, so the warp follows the downhill
direction of what is already composed beneath the layer. Two sources, both pointing downhill:

```text
NormalFlow = decoded tangent normal .xy at this pixel
HeightFlow = -gradient(accumulated height, WarpRadius)

Flow     = lerp(NormalFlow, HeightFlow, WarpSource)
SoftFlow = Flow / (1 + length(Flow))
WarpUV   = UV + SoftFlow * WarpAmount
```

`WarpSource` 0 uses the normal map's own slope, which is high-frequency and follows surface
detail. 1 uses the composited height gradient, which is coarser and follows real spatial
relief. `WarpRadius` sets the gradient sample distance in pixels.

Magnitude is softened rather than normalized. Displacement stays proportional to slope on
gentle ground and rolls off asymptotically on steep ground, so the warp feathers out across
flat regions instead of displacing every pixel equally. Direction is preserved exactly, and
`WarpAmount = 0` must be bit-identical to the unwarped path.

Do not warp by sampling the normal at a scaled UV. That borrows an unrelated point on the
surface and produces decorative noise rather than flow.

### Mix

```text
Raw = CurvatureWeight * lerp(Cavity, Convex, CurvatureBias)
    + DirectionWeight * Direction
    + AOWeight        * (1 - PreviousPacked.g)
    + HeightWeight    * saturate(PreviousHeight + HeightBias)

Total = |CurvatureWeight| + |DirectionWeight| + |AOWeight| + |HeightWeight|

Mixed = bNormalizeWeights && Total > epsilon
    ? Raw / Total
    : Raw

Generated = MaterialLabBias01(saturate(Mixed), Bias)
```

Normalized weights behave as a blend; unnormalized weights accumulate and clip. Default is
normalized so that raising one weight does not wash out the others.

The result then enters the existing accumulator unchanged:

```text
Incoming = Generated
Incoming = saturate((Incoming - 0.5) * Contrast + 0.5 + Offset)
Incoming = Balance(Incoming)
Incoming = Invert ? 1 - Incoming : Incoming
Accumulated = saturate(lerp(Accumulated, Operation(Accumulated, Incoming), Weight))
```

## Implementation direction

1. Add the child type and struct; keep `Mask` and `Effect` values unchanged.
2. Add `Shaders/Private/MaterialLabGeneratedMask.usf` as a compute pass writing the same
   `RWTexture2D<float> OutputMask` as `MaterialLabMask.usf`.
3. Reuse `MaterialLabCurvature.ush` for the curvature term. Do not duplicate the evaluator.
4. Bind the accumulated Normal, packed RAM, and Height targets into the mask pass. This is
   the only structural change: the mask ping-pong pass currently binds textures only.
5. Dispatch generated children in stored child order alongside texture masks, reusing the
   two existing mask ping-pong targets. Do not add a per-child target.
6. Extend `EMaterialLabDebugPreviewMode` so a selected generated child previews in isolation.
7. Leave `FeatureInfluence`, `AOFeatureInfluence`, and `HeightFeatureInfluence` serialized and
   functional. Old recipes must render unchanged.

Do not modify the protected master. Do not add a fourth GPU pass per child. Do not evaluate
generated masks at runtime — this is editor-time composition that bakes into the final maps.

## Cost

Per generated child: four normal taps for curvature, one AO tap, one height tap, plus two
taps when warp is enabled. Terms with zero weight must be branched out entirely, so a
direction-only node costs one tap.

## Limitations

- Direction keys off slope, so a flat region reads neutral rather than high. This is
  deliberate: on a brick surface the flat faces must stay clean while dust collects in
  the mortar and against the up-facing chamfers. Do not add an `N.z` flatness term.
- Curvature inherits its radius artifacts and its sensitivity to compressed or noisy normals.
- Height quality varies with provenance: authored `_RAMH` is exact, normal-derived height is
  an approximation and is itself still unverified.
- Slope flow follows the surface beneath, so it cannot produce variation independent of that
  surface. Large-scale independent break-up needs a noise source, which this node does not have.

## Validation

1. A node with all weights `0` leaves the accumulated mask unchanged.
2. Each signal in isolation matches the equivalent per-layer scalar path.
3. Normalized and unnormalized mixes agree when exactly one weight is non-zero.
4. `WarpAmount = 0` is bit-identical to the unwarped path.
4a. Warp displaces downhill for both sources, and agrees between them on a known ramp.
5. Direction at 90° concentrates on +Y slopes; at 270° it inverts.
6. Inverted AO concentrates in occluded regions of the accumulated surface.
7. Generated and texture mask children interleave in stored order.
8. Blend modes, Balance, Contrast, Offset, and Invert behave as for texture masks.
9. Recipes saved with generated children reopen with identical order and values.
10. Legacy recipes using the three per-layer scalars render unchanged.
11. The generated mask drives BC, Normal, Roughness, AO, Metallic, F0, and Height together.
12. Baked output matches the preview.

Do not run builds, Unreal, or tests without explicit permission.
