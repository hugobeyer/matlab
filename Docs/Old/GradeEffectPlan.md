# Material Lab — Grade Effect

Status: Implemented, not visually verified in the editor.

## What it is

A colour grade: tonemap, gamma, brightness and contrast over the composited base colour, gated
by the owning layer's mask.

It is a **Filter**, the second one after erosion. It reads a channel that already exists and
returns a transformed version of it, and at `Amount = 0` it is the identity.

## Why it is a post-layer filter

Grade runs after its owning layer composites, exactly like erosion and for the same reason:
running it inside the child loop would grade a base colour the layer then paints straight over.

The consequence is the useful part. Because it runs after the composite, it grades the surface
the whole stack has accumulated *up to that layer*, not one layer's source texture. Masked, that
makes it an adjustment layer — drop it on a layer, mask it, and everything underneath is graded
where the mask says.

On a layer carrying both filters, grade runs after erosion, so it grades the surface erosion has
already shaded rather than the one it was about to.

## Order of operations

```text
Brightness  ->  Contrast (about Pivot)  ->  Tonemap  ->  Gamma
```

The order is the point, not an implementation detail:

- **Brightness and contrast are linear operations** and belong above the tonemap. Applied after
  one they fight a curve that has already decided where the highlights went.
- **Gamma is display shaping** and belongs below the tonemap.

Get this backwards and every control still appears to do something, which is why it is written
down rather than left to the reader of the shader.

## The individual choices

**Brightness is a gain, not an offset.** These are linear values, so scaling behaves like
exposure and leaves channel ratios — and therefore hue — intact. Adding a constant washes
saturation out of the darks.

**Contrast pivots about a control, not a constant.** `(c - Pivot) * Contrast + Pivot`. The
default is `0.18`, linear mid grey, which is correct for this data. `0.5` is what
display-referred habits reach for, and forcing either one silently would be wrong half the time.

**Gamma is `pow(c, 1 / Gamma)`,** so a value above 1 lifts the midtones. That is the convention
every grading UI uses and the reciprocal is easy to get backwards.

**Tonemap has a strength**, so an operator can be dialled in rather than only switched on.

```text
None      identity
Reinhard  c / (1 + c). Never clips, but desaturates highlights and reaches white only at
          infinity, so bright areas go pale rather than bright.
ACES      Narkowicz's fit to the RRT/ODT. Contrastier, filmic toe, and the closest of the
          three to what a renderer will do to this surface later.
Filmic    Hable's Uncharted 2 curve, divided by its own value at the white point. Without
          that normalisation the curve maps 1.0 to about 0.79 and everything reads dark.
```

## Scope

**Base colour only.** Roughness and metallic are not colours and do not want a tonemap; a
roughness adjustment is a different filter with different controls. The erosion shade pass
already offsets roughness where that is wanted locally.

## Masking

The grade reads the layer's accumulated mask children — the same `CombinedMask` the layer
composite uses — so mask children, generated masks and the layer's own mask all steer it through
one path. `Invert` grades everything the mask does not cover. A layer with no mask grades
everywhere, since `HasMask` is false and the weight falls back to 1.

## Exposed controls

```text
Amount      0..1        blend against the ungraded color; 0 is the identity
Brightness  0..4        linear gain
Contrast    0..4        scales distance from the pivot; 0 flattens to the pivot
Pivot       0..1        the value contrast pivots about; 0.18 is linear mid grey
Operator    None|Reinhard|ACES|Filmic
Strength    0..1        blend between untonemapped and tonemapped
Gamma       0.05..4     pow(c, 1/Gamma); above 1 lifts midtones
Invert      toggle      grade what the mask does not cover
```

## Implementation

```text
Shaders/Private/MixtormatGrade.usf   the whole filter, one dispatch
FMixtormatGradeCS                    shader class, render data, gather
PendingGrade                         deferred out of the child loop like PendingErosion
```

The pass reads and writes the layer's base colour target, which cannot be bound as SRV and UAV
in the same dispatch, so it writes a scratch texture and copies back — the same shape the
erosion shade pass uses.

**Grades stack.** Erosion keeps a single pending pointer because two erosions on one layer is
nonsense; two grades is not — a brightness pass and a separate tonemap pass is an ordinary way
to use an adjustment layer. They run in child order, each reading what the last wrote.

**The gather gates on the resolved type, not on the absence of an asset.** Erosion got away with
the narrower test because nothing creates Erosion assets, but `Grade` is a valid `EffectType` on
`UMixtormatEffect`, so an authored Grade asset would otherwise fall through into the peel
branches and set `bHasEffects` — making the composite sample an effect-data buffer nothing wrote.

**The mask is sampled by UV while the colour is loaded by pixel.** Deliberate: the colour target
is always at composition resolution, but with no mask children the mask is the layer's authored
texture at whatever resolution the asset is. Loading it would read the wrong texel.

Negative source values are clamped to zero on the way in, since `pow` returns NaN for them and
they are not meaningful base colour. The final blend still returns the untouched source, so a
negative pixel survives `Amount = 0` intact.

## Validation

1. `Amount = 0` is bit-identical to the ungraded base colour.
2. Default controls with `Operator = None` are the identity at `Amount = 1`.
3. `Gamma = 2` brightens midtones; `Gamma = 0.5` darkens them.
4. `Contrast = 0` flattens the masked region to the pivot value.
5. A masked grade leaves the unmasked region bit-identical; `Invert` swaps which region moves.
6. A layer with no mask children grades everywhere.
7. Grade on a layer with erosion runs after it, over the eroded and shaded result.
8. Baked output matches the preview.

Do not run builds, Unreal, or tests without explicit permission.
