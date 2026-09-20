# Mixtormat — Replace Chipping with Breakup

Work from the latest `hugobeyer/matlab` main branch.

## Goal

Completely replace the existing **Chipping** effect with a new procedural effect called:

**Breakup**

Chipping is not being preserved as a second effect.

Breakup occupies Chipping's existing effect slot and replaces its implementation.

The new effect is a general-purpose procedural SDF breakup system capable of producing:

* torn surfaces
* peeled/flaking material
* cracks
* fractured plates
* chipped rock/concrete
* raised rock foundations
* erosion islands
* SDF push warping
* layered damage
* corrosion breakup
* mud/ice plates
* bark-like breakup
* embossed/raised fragment fields

The core is based on a tileable multi-scale SDF composed from irregular cells.

---

# Critical migration rule

Current enum:

```cpp
Chipping = 4 UMETA(DisplayName = "Chipping"),
```

Replace it with:

```cpp
Breakup = 4 UMETA(DisplayName = "Breakup"),
```

DO NOT append Breakup at the end.

Keep numeric value `4`.

Existing recipes already serialize this slot.

Also add an enum redirect for assets that serialized the enum value by name.

Create the appropriate plugin config, preferably:

```text
Config/DefaultMixtormat.ini
```

with:

```ini
[CoreRedirects]
+EnumRedirects=(OldName="/Script/MixtormatRuntime.EMixtormatEffectType",ValueChanges=(("Chipping","Breakup")))
```

Do not retain a functional `Chipping` enum entry.

Old Chipping assets should load as Breakup.

---

# Remove the old Chipping implementation

The existing Chipping algorithm is unwanted.

Remove from the active path:

* iterative chip growth
* chip tip propagation
* `ChipIterations`
* state ping-pong
* grout threshold logic
* height min/max reduction used only by Chipping
* cavity-driven chip seeding
* the old Chipping normal pass
* `FMixtormatChippingCS`
* `MixtormatChipping.usf`
* Chipping labels/tooltips/menu names
* `GetSelectedChipping`
* `AddChippingToLayer`
* `BuildChippingControls`
* `PendingChipping`
* Chipping RDG event names
* old Chipping debug labels

If `FMixtormatReduceMinMaxCS` becomes completely unused after Chipping is removed, delete it.

Old `Chip*` serialized properties may remain temporarily as hidden `DeprecatedProperty` fields if necessary for asset compatibility, but:

* they must not appear in UI
* they must not drive rendering
* they must not be copied into Breakup parameters
* do not preserve old Chipping visual behavior

Search the entire repository for:

```text
Chipping
ChipAmount
ChipSize
ChipDepth
ChipIterations
ChipGrout
ChipCavity
ChipHeight
ChipMask
ChipSeed
```

and clean the active implementation completely.

---

# Architecture

Breakup should be a **two-pass GPU effect**, followed by Mixtormat's existing height-derived normal reconciliation.

```text
PASS 1 — Breakup Field
    procedural tileable SDF
    + macro/mid/detail families
    + union/subtract/intersect
    + distortion
    + stable generated piece identity
              ↓
       RG16F field texture
       R = signed distance
       G = stable per-piece random

PASS 2 — Apply
       SDF field
       + current height
       + placement/scoped mask
              ↓
       signed relief
       + fold/lip
       + crease
       + SDF push warp
              ↓
       output height
       output coverage

Then:
    existing AddHeightDerivedNormalPass()

Optional:
    existing CarveShade-style roughness adjustment
    using Breakup coverage
```

There must be no iterative simulation.

---

# Hybrid ID behavior

Breakup always generates an internal stable piece ID from its procedural cells.

The artist should NOT need an ID texture for Breakup to work.

Internally:

```text
generated cell
    ↓
stable uint ID
    ↓
stable random value
    ↓
per-piece relief/fold/push variation
```

If the current layer already has Mixtormat Pattern IDs available, optionally mix them into the generated ID:

```cpp
FinalId = Hash(
    GeneratedId ^
    PatternRegionId * Prime);
```

This gives stable per-brick/per-tile variation while still allowing Breakup to produce its own sub-fragments.

Pattern IDs must remain optional.

Do not add a mandatory external ID picker in this first implementation.

Reuse the existing safe `RegionIds` binding/fallback behavior already used by Worn Edges if Pattern IDs are integrated.

---

# Runtime enums

Add:

```cpp
UENUM(BlueprintType)
enum class EMixtormatBreakupOperation : uint8
{
    Union = 0 UMETA(DisplayName = "Union"),
    Subtract = 1 UMETA(DisplayName = "Subtract"),
    Intersect = 2 UMETA(DisplayName = "Intersect")
};
```

Replace:

```cpp
EMixtormatEffectType::Chipping
```

with:

```cpp
EMixtormatEffectType::Breakup
```

everywhere.

Breakup remains a Filter:

```cpp
case EMixtormatEffectType::Breakup:
    return EMixtormatEffectClass::Filter;
```

---

# Runtime parameters

Add proper `Breakup*` properties to `FMixtormatLayerEffect`.

Suggested data:

```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakup")
int32 BreakupScale = 6;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakup")
float BreakupDetail = 0.5f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakup")
float BreakupDensity = 0.72f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakup")
float BreakupSize = 0.32f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakup")
float BreakupSizeVariation = 0.3125f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakup")
float BreakupStretch = 1.6f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakup")
float BreakupAngularity = 0.72f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakup")
float BreakupIrregularity = 0.38f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakup|Advanced")
EMixtormatBreakupOperation BreakupMidOperation =
    EMixtormatBreakupOperation::Union;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakup|Advanced")
EMixtormatBreakupOperation BreakupDetailOperation =
    EMixtormatBreakupOperation::Union;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakup|Advanced")
float BreakupSmoothness = 0.30f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakup")
float BreakupDistortion = 5.6f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakup|Advanced")
int32 BreakupDistortionFrequency = 3;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakup")
float BreakupRelief = -0.06f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakup")
float BreakupFold = 0.025f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakup|Advanced")
float BreakupFoldWidth = 16.0f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakup")
float BreakupCrease = 0.018f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakup|Advanced")
float BreakupCreaseWidth = 1.25f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakup")
float BreakupPush = 0.0f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakup|Advanced")
float BreakupPushWidth = 24.0f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakup")
float BreakupVariation = 0.25f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakup")
float BreakupAmount = 1.0f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakup")
float BreakupRoughnessAmount = 0.0f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakup")
bool bBreakupInvert = false;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breakup")
int32 BreakupSeed = 1;
```

Retain the existing optional/scoped mask semantics.

Rename the old Chipping-owned mask fields appropriately if that dedicated placement-mask feature is retained:

```cpp
BreakupMask
BreakupMaskTexture
BreakupMaskTiling
bBreakupInvertMask
```

---

# Derived frequencies

Do not expose three cell-count sliders by default.

Derive Mid and Detail from Scale + Detail.

At the default values these should approximately reproduce:

```text
Macro  = 6
Mid    = 11
Detail = 20
```

For example:

```cpp
const int32 MacroCells =
    FMath::Max(BreakupScale, 1);

const float Detail =
    FMath::Clamp(BreakupDetail, 0.0f, 1.0f);

const int32 MidCells =
    FMath::Max(
        MacroCells + 1,
        FMath::RoundToInt(
            MacroCells *
            FMath::Lerp(1.50f, 2.15f, Detail)));

const int32 DetailCells =
    FMath::Max(
        MidCells + 1,
        FMath::RoundToInt(
            MacroCells *
            FMath::Lerp(2.60f, 4.10f, Detail)));
```

Size range:

```cpp
const float SizeMin =
    BreakupSize *
    (1.0f - BreakupSizeVariation);

const float SizeMax =
    BreakupSize *
    (1.0f + BreakupSizeVariation);
```

Clamp to safe positive values before sending to GPU.

---

# Shader

Delete:

```text
Shaders/Private/MixtormatChipping.usf
```

Add:

```text
Shaders/Private/MixtormatBreakup.usf
```

Use this implementation as the starting point.

Do not replace this with the old Chipping algorithm.

```hlsl
// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "/Engine/Public/Platform.ush"

#define BREAKUP_TAU 6.28318530717958647692f
#define BREAKUP_REFERENCE_RES 1024.0f

Texture2D<uint> RegionIds;
Texture2D<float> SourceHeight;
Texture2D<float2> BreakupField;
Texture2D<float> LayerMask;
Texture2D<float> PlacementMaskTexture;

SamplerState LinearWrapSampler;

RWTexture2D<float2> OutputField;
RWTexture2D<float> OutputHeight;
RWTexture2D<float> OutputCoverage;

int2 OutputSize;

uint Seed;

int MacroCells;
int MidCells;
int DetailCells;

float Density;
float SizeMin;
float SizeMax;
float Stretch;
float Angularity;
float Jitter;

int OperationMid;
int OperationDetail;
float BlendSmooth;

float DistortAmount;
int DistortFrequency;

uint InvertField;

uint HasRegionIds;

float Relief;
float FoldHeight;
float FoldWidth;
float CreaseWidth;
float CreaseDepth;

float PushAmount;
float PushWidth;

float Variation;
float Strength;

uint UsePlacementMask;
float PlacementMaskTiling;
uint InvertMask;


uint BreakupHashU(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}


uint BreakupHashCoord(int x, int y, uint s)
{
    return BreakupHashU(
        (uint)x * 73856093u ^
        (uint)y * 19349663u ^
        s * 83492791u);
}


float BreakupHash01(uint x)
{
    return
        (float)(BreakupHashU(x) & 0x00ffffffu) *
        (1.0f / 16777216.0f);
}


int BreakupWrapI(int x, int n)
{
    const int r = x % n;
    return r < 0 ? r + n : r;
}


int2 BreakupWrapPixel(int2 p)
{
    return int2(
        BreakupWrapI(p.x, OutputSize.x),
        BreakupWrapI(p.y, OutputSize.y));
}


float BreakupSmooth(float a, float b, float x)
{
    const float t =
        saturate(
            (x - a) /
            max(b - a, 1.0e-6f));

    return
        t * t *
        (3.0f - 2.0f * t);
}


float BreakupSMin(float a, float b, float k)
{
    if (k <= 1.0e-6f)
    {
        return min(a, b);
    }

    const float h =
        saturate(
            0.5f +
            0.5f *
            (b - a) /
            k);

    return
        lerp(b, a, h) -
        k * h *
        (1.0f - h);
}


float BreakupSMax(float a, float b, float k)
{
    return
        -BreakupSMin(
            -a,
            -b,
            k);
}


float2 BreakupRotate(float2 p, float a)
{
    const float c = cos(a);
    const float s = sin(a);

    return float2(
        c * p.x - s * p.y,
        s * p.x + c * p.y);
}


float2 BreakupDistort(
    float2 uv,
    int frequency,
    float amount,
    uint seed)
{
    const int f =
        max(frequency, 1);

    const float p0 =
        BreakupHash01(seed + 11u) *
        BREAKUP_TAU;

    const float p1 =
        BreakupHash01(seed + 31u) *
        BREAKUP_TAU;

    const float p2 =
        BreakupHash01(seed + 59u) *
        BREAKUP_TAU;

    const float p3 =
        BreakupHash01(seed + 89u) *
        BREAKUP_TAU;

    const float x =
        sin(
            BREAKUP_TAU *
            uv.y *
            (float)f +
            p0)
        +
        0.45f *
        sin(
            BREAKUP_TAU *
            (
                uv.x *
                (float)(f + 1) +
                uv.y *
                (float)f
            ) +
            p1);

    const float y =
        sin(
            BREAKUP_TAU *
            uv.x *
            (float)f +
            p2)
        +
        0.45f *
        sin(
            BREAKUP_TAU *
            (
                uv.x *
                (float)f -
                uv.y *
                (float)(f + 1)
            ) +
            p3);

    uv +=
        float2(x, y) *
        (
            amount /
            BREAKUP_REFERENCE_RES
        );

    return frac(uv);
}


float BreakupShape(
    float2 q,
    float sx,
    float sy,
    float angularity,
    float shear)
{
    q.x += q.y * shear;

    const float ax =
        abs(q.x) /
        max(sx, 1.0e-6f);

    const float ay =
        abs(q.y) /
        max(sy, 1.0e-6f);

    const float boxField =
        max(ax, ay);

    const float diamond =
        ax + ay;

    const float metric =
        lerp(
            boxField,
            diamond,
            saturate(angularity));

    return
        (metric - 1.0f) *
        min(sx, sy);
}


struct FBreakupSample
{
    float Distance;
    uint Id;
};


FBreakupSample BreakupFamily(
    float2 uv,
    int cells,
    uint seed,
    float density,
    float sizeMin,
    float sizeMax,
    float stretchAmount,
    float angularity,
    float jitter)
{
    FBreakupSample result;

    result.Distance = 1000.0f;
    result.Id = 0u;

    cells =
        max(cells, 1);

    const float2 p =
        uv *
        (float)cells;

    const int bx =
        (int)floor(p.x);

    const int by =
        (int)floor(p.y);

    [unroll]
    for (int oy = -1; oy <= 1; ++oy)
    {
        [unroll]
        for (int ox = -1; ox <= 1; ++ox)
        {
            const int gx =
                bx + ox;

            const int gy =
                by + oy;

            const int hx =
                BreakupWrapI(
                    gx,
                    cells);

            const int hy =
                BreakupWrapI(
                    gy,
                    cells);

            const uint id =
                BreakupHashCoord(
                    hx,
                    hy,
                    seed);

            const float active =
                BreakupHash01(
                    id ^
                    0x9e3779b9u);

            if (active > density)
            {
                continue;
            }

            const float rx =
                BreakupHash01(
                    id ^
                    0x243f6a88u);

            const float ry =
                BreakupHash01(
                    id ^
                    0x85a308d3u);

            const float rs =
                BreakupHash01(
                    id ^
                    0x13198a2eu);

            const float ra =
                BreakupHash01(
                    id ^
                    0x03707344u);

            const float rt =
                BreakupHash01(
                    id ^
                    0xa4093822u);

            const float rg =
                BreakupHash01(
                    id ^
                    0x299f31d0u);

            const float rang =
                BreakupHash01(
                    id ^
                    0x082efa98u);

            const float rsh =
                BreakupHash01(
                    id ^
                    0xec4e6c89u);

            const float2 center =
                float2(
                    (float)gx +
                    0.5f +
                    (rx - 0.5f) *
                    jitter,

                    (float)gy +
                    0.5f +
                    (ry - 0.5f) *
                    jitter
                ) /
                (float)cells;

            float2 q =
                uv -
                center;

            q =
                BreakupRotate(
                    q,
                    ra *
                    BREAKUP_TAU);

            const float radius =
                lerp(
                    sizeMin,
                    sizeMax,
                    rs) /
                (float)cells;

            float stretch =
                lerp(
                    1.0f,
                    max(
                        stretchAmount,
                        1.0f),
                    rt);

            if (rg > 0.5f)
            {
                stretch =
                    1.0f /
                    stretch;
            }

            const float sx =
                radius *
                stretch;

            const float sy =
                radius /
                stretch;

            const float ang =
                saturate(
                    angularity +
                    (rang - 0.5f) *
                    0.30f);

            const float shear =
                (rsh - 0.5f) *
                0.75f;

            const float d =
                BreakupShape(
                    q,
                    sx,
                    sy,
                    ang,
                    shear);

            if (d < result.Distance)
            {
                result.Distance = d;
                result.Id = id;
            }
        }
    }

    return result;
}


FBreakupSample BreakupCombine(
    FBreakupSample a,
    FBreakupSample b,
    float k,
    int operation)
{
    FBreakupSample result;

    if (operation == 1)
    {
        const float db =
            -b.Distance;

        result.Distance =
            BreakupSMax(
                a.Distance,
                db,
                k);

        result.Id =
            a.Distance >= db
            ? a.Id
            : b.Id;

        return result;
    }

    if (operation == 2)
    {
        result.Distance =
            BreakupSMax(
                a.Distance,
                b.Distance,
                k);

        result.Id =
            a.Distance >= b.Distance
            ? a.Id
            : b.Id;

        return result;
    }

    result.Distance =
        BreakupSMin(
            a.Distance,
            b.Distance,
            k);

    result.Id =
        a.Distance <= b.Distance
        ? a.Id
        : b.Id;

    return result;
}


float BreakupSamplePlacement(float2 uv)
{
    float value;

    if (UsePlacementMask != 0u)
    {
        value =
            PlacementMaskTexture.SampleLevel(
                LinearWrapSampler,
                frac(
                    uv *
                    max(
                        PlacementMaskTiling,
                        1.0f)),
                0.0f);
    }
    else
    {
        value =
            LayerMask.SampleLevel(
                LinearWrapSampler,
                uv,
                0.0f);
    }

    if (InvertMask != 0u)
    {
        value =
            1.0f -
            value;
    }

    return saturate(value);
}


float BreakupLoadSDF(int2 pixel)
{
    return
        BreakupField.Load(
            int3(
                BreakupWrapPixel(pixel),
                0)).x;
}


[numthreads(8, 8, 1)]
void FieldCS(
    uint3 DispatchThreadId :
    SV_DispatchThreadID)
{
    if (
        any(
            DispatchThreadId.xy >=
            uint2(OutputSize)))
    {
        return;
    }

    const int2 pixel =
        int2(
            DispatchThreadId.xy);

    float2 uv =
        (
            float2(pixel) +
            0.5f
        ) /
        float2(OutputSize);

    uv =
        BreakupDistort(
            uv,
            DistortFrequency,
            DistortAmount,
            Seed + 1001u);

    const int mc =
        max(
            MacroCells,
            1);

    const int md =
        max(
            MidCells,
            1);

    const int dc =
        max(
            DetailCells,
            1);

    FBreakupSample d0 =
        BreakupFamily(
            uv,
            mc,
            Seed + 11u,
            Density,
            SizeMin,
            SizeMax,
            Stretch,
            Angularity,
            Jitter);

    FBreakupSample d1 =
        BreakupFamily(
            uv,
            md,
            Seed + 107u,
            saturate(
                Density *
                0.92f),
            SizeMin *
                0.85f,
            SizeMax *
                0.95f,
            Stretch,
            Angularity,
            Jitter);

    FBreakupSample d2 =
        BreakupFamily(
            uv,
            dc,
            Seed + 251u,
            saturate(
                Density *
                0.72f),
            SizeMin *
                0.65f,
            SizeMax *
                0.82f,
            Stretch,
            saturate(
                Angularity +
                0.08f),
            Jitter);

    FBreakupSample result =
        BreakupCombine(
            d0,
            d1,
            BlendSmooth /
                (float)mc,
            OperationMid);

    result =
        BreakupCombine(
            result,
            d2,
            BlendSmooth /
                (float)md,
            OperationDetail);

    float sd =
        result.Distance *
        BREAKUP_REFERENCE_RES;

    if (InvertField != 0u)
    {
        sd =
            -sd;
    }

    uint finalId =
        result.Id;

    if (HasRegionIds != 0u)
    {
        const uint regionId =
            RegionIds.Load(
                int3(
                    pixel,
                    0));

        finalId =
            BreakupHashU(
                finalId ^
                regionId *
                0x9e3779b9u);
    }

    const float idRandom =
        BreakupHash01(
            finalId ^
            Seed *
            0x85ebca6bu);

    OutputField[pixel] =
        float2(
            clamp(
                sd,
                -4096.0f,
                4096.0f),
            idRandom);
}


[numthreads(8, 8, 1)]
void ApplyCS(
    uint3 DispatchThreadId :
    SV_DispatchThreadID)
{
    if (
        any(
            DispatchThreadId.xy >=
            uint2(OutputSize)))
    {
        return;
    }

    const int2 pixel =
        int2(
            DispatchThreadId.xy);

    const float2 uv =
        (
            float2(pixel) +
            0.5f
        ) /
        float2(OutputSize);

    const float sourceHeight =
        SourceHeight.Load(
            int3(
                pixel,
                0));

    const float2 field =
        BreakupField.Load(
            int3(
                pixel,
                0));

    const float sd =
        field.x;

    const float r0 =
        field.y;

    const float r1 =
        frac(
            r0 *
            1.61803398875f +
            0.173f);

    const float r2 =
        frac(
            r0 *
            2.41421356237f +
            0.517f);

    const float r3 =
        frac(
            r0 *
            3.14159265359f +
            0.731f);

    const float placement =
        BreakupSamplePlacement(
            uv);

    const float reliefWidth =
        max(
            CreaseWidth *
            2.0f,
            1.0f);

    const float inside =
        BreakupSmooth(
            0.0f,
            reliefWidth,
            -sd);

    float fold =
        0.0f;

    if (sd >= 0.0f)
    {
        fold =
            1.0f -
            BreakupSmooth(
                0.0f,
                max(
                    FoldWidth,
                    1.0e-4f),
                sd);

        fold =
            fold *
            fold *
            (
                3.0f -
                2.0f *
                fold
            );
    }

    const float crease =
        1.0f -
        BreakupSmooth(
            0.0f,
            max(
                CreaseWidth,
                1.0e-4f),
            abs(sd));

    const float pushBand =
        1.0f -
        BreakupSmooth(
            0.0f,
            max(
                PushWidth,
                1.0e-4f),
            abs(sd));

    const float sdfL =
        BreakupLoadSDF(
            pixel +
            int2(-1, 0));

    const float sdfR =
        BreakupLoadSDF(
            pixel +
            int2(1, 0));

    const float sdfD =
        BreakupLoadSDF(
            pixel +
            int2(0, -1));

    const float sdfU =
        BreakupLoadSDF(
            pixel +
            int2(0, 1));

    float2 gradient =
        float2(
            sdfR - sdfL,
            sdfU - sdfD);

    const float gradientLength =
        length(gradient);

    gradient =
        gradientLength >
            1.0e-5f
        ? gradient /
            gradientLength
        : float2(
            0.0f,
            0.0f);

    const float pieceVariation =
        lerp(
            1.0f -
                saturate(Variation),
            1.0f +
                saturate(Variation),
            r0);

    const float foldVariation =
        lerp(
            1.0f -
                saturate(Variation) *
                0.5f,
            1.0f +
                saturate(Variation) *
                0.5f,
            r1);

    const float creaseVariation =
        lerp(
            1.0f -
                saturate(Variation) *
                0.35f,
            1.0f +
                saturate(Variation) *
                0.35f,
            r2);

    const float pushVariation =
        lerp(
            1.0f -
                saturate(Variation) *
                0.5f,
            1.0f +
                saturate(Variation) *
                0.5f,
            r3);

    const float side =
        sd >= 0.0f
        ? 1.0f
        : -1.0f;

    const float pushUV =
        PushAmount /
        BREAKUP_REFERENCE_RES;

    const float2 warpedUV =
        frac(
            uv +
            gradient *
            side *
            pushUV *
            pushBand *
            pushVariation);

    float warpedHeight =
        sourceHeight;

    if (abs(PushAmount) > 1.0e-6f)
    {
        warpedHeight =
            SourceHeight.SampleLevel(
                LinearWrapSampler,
                warpedUV,
                0.0f);
    }

    const float delta =
        inside *
            Relief *
            pieceVariation
        +
        fold *
            FoldHeight *
            foldVariation
        -
        crease *
            CreaseDepth *
            creaseVariation;

    const float amount =
        saturate(
            Strength *
            placement);

    const float result =
        lerp(
            sourceHeight,
            warpedHeight +
                delta,
            amount);

    OutputHeight[pixel] =
        saturate(result);

    OutputCoverage[pixel] =
        saturate(
            max(
                inside,
                max(
                    fold,
                    max(
                        crease,
                        pushBand))) *
            amount);
}
```

---

# Shader C++ classes

Replace `FMixtormatChippingCS` with two shaders.

Conceptually:

```cpp
class FMixtormatBreakupFieldCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FMixtormatBreakupFieldCS);
    SHADER_USE_PARAMETER_STRUCT(FMixtormatBreakupFieldCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntPoint, OutputSize)
        SHADER_PARAMETER(uint32, Seed)

        SHADER_PARAMETER(int32, MacroCells)
        SHADER_PARAMETER(int32, MidCells)
        SHADER_PARAMETER(int32, DetailCells)

        SHADER_PARAMETER(float, Density)
        SHADER_PARAMETER(float, SizeMin)
        SHADER_PARAMETER(float, SizeMax)
        SHADER_PARAMETER(float, Stretch)
        SHADER_PARAMETER(float, Angularity)
        SHADER_PARAMETER(float, Jitter)

        SHADER_PARAMETER(int32, OperationMid)
        SHADER_PARAMETER(int32, OperationDetail)
        SHADER_PARAMETER(float, BlendSmooth)

        SHADER_PARAMETER(float, DistortAmount)
        SHADER_PARAMETER(int32, DistortFrequency)

        SHADER_PARAMETER(uint32, InvertField)

        SHADER_PARAMETER(uint32, HasRegionIds)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionIds)

        SHADER_PARAMETER_RDG_TEXTURE_UAV(
            RWTexture2D<float2>,
            OutputField)
    END_SHADER_PARAMETER_STRUCT()
};
```

```cpp
IMPLEMENT_GLOBAL_SHADER(
    FMixtormatBreakupFieldCS,
    "/Plugin/Mixtormat/Private/MixtormatBreakup.usf",
    "FieldCS",
    SF_Compute);
```

Second shader:

```cpp
class FMixtormatBreakupApplyCS final : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FMixtormatBreakupApplyCS);
    SHADER_USE_PARAMETER_STRUCT(FMixtormatBreakupApplyCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FIntPoint, OutputSize)

        SHADER_PARAMETER(float, Relief)

        SHADER_PARAMETER(float, FoldHeight)
        SHADER_PARAMETER(float, FoldWidth)

        SHADER_PARAMETER(float, CreaseWidth)
        SHADER_PARAMETER(float, CreaseDepth)

        SHADER_PARAMETER(float, PushAmount)
        SHADER_PARAMETER(float, PushWidth)

        SHADER_PARAMETER(float, Variation)
        SHADER_PARAMETER(float, Strength)

        SHADER_PARAMETER(uint32, UsePlacementMask)
        SHADER_PARAMETER(float, PlacementMaskTiling)
        SHADER_PARAMETER(uint32, InvertMask)

        SHADER_PARAMETER_RDG_TEXTURE(
            Texture2D<float>,
            SourceHeight)

        SHADER_PARAMETER_RDG_TEXTURE(
            Texture2D<float2>,
            BreakupField)

        SHADER_PARAMETER_RDG_TEXTURE(
            Texture2D<float>,
            LayerMask)

        SHADER_PARAMETER_RDG_TEXTURE(
            Texture2D<float>,
            PlacementMaskTexture)

        SHADER_PARAMETER_SAMPLER(
            SamplerState,
            LinearWrapSampler)

        SHADER_PARAMETER_RDG_TEXTURE_UAV(
            RWTexture2D<float>,
            OutputHeight)

        SHADER_PARAMETER_RDG_TEXTURE_UAV(
            RWTexture2D<float>,
            OutputCoverage)
    END_SHADER_PARAMETER_STRUCT()
};
```

```cpp
IMPLEMENT_GLOBAL_SHADER(
    FMixtormatBreakupApplyCS,
    "/Plugin/Mixtormat/Private/MixtormatBreakup.usf",
    "ApplyCS",
    SF_Compute);
```

Use the project's normal:

```cpp
static bool ShouldCompilePermutation(
    const FGlobalShaderPermutationParameters& Parameters)
{
    return IsFeatureLevelSupported(
        Parameters.Platform,
        ERHIFeatureLevel::SM5);
}
```

---

# RDG texture layout

Field:

```cpp
FRDGTextureDesc::Create2D(
    Request.Resolution,
    PF_G16R16F,
    FClearValueBinding::Black,
    TexCreate_ShaderResource |
    TexCreate_UAV);
```

Meaning:

```text
R = signed distance
G = stable per-piece random
```

Do not allocate a full R32_UINT ID texture just to vary fragments.

That would waste a substantial amount of transient memory at 4K.

The stable generated `uint Id` only needs to exist during `FieldCS`; hash it into the second field channel.

Coverage:

```cpp
PF_R16F
```

Height:

reuse the existing height scratch/output arrangement.

---

# GPU pipeline

Implement something structurally like:

```text
Source composited height
        ↓
Breakup FieldCS
        ↓
BreakupField RG16F
        ↓
Breakup ApplyCS
        ↓
Modified height + coverage
        ↓
AddHeightDerivedNormalPass
        ↓
optional roughness pass using coverage
```

No reduction.

No iterations.

No state ping-pong.

No previous-state texture.

No chip-tip propagation.

---

# Normal handling

Height is authoritative.

Do not create another independent procedural normal algorithm.

After Breakup modifies height:

```cpp
AddHeightDerivedNormalPass(...)
```

using:

```text
old height
new Breakup height
previous normal
```

the same way the current structural effects reconcile normals.

This keeps normals consistent with actual resulting height.

---

# Roughness

Breakup may retain an optional:

```cpp
BreakupRoughnessAmount
```

Use `OutputCoverage` as the coverage texture.

Reuse the existing simple roughness shading pass if possible.

Do not author Base Color.

Do not bake colour into Breakup.

---

# Mask semantics

Breakup must retain Mixtormat's normal effect scoping behavior.

It must work with:

* layer child mask
* scoped effect mask
* optional Breakup-specific placement mask if retained
* invert mask
* nested effect chains

A scoped mask should override the effect's own inversion behavior in exactly the same way current effects do.

Do not allow Breakup to leak outside its resolved placement mask.

---

# Artist UI

Remove the Chipping panel entirely.

Add:

```text
BREAKUP
```

Suggested layout:

```text
SHAPE
Scale             Density
Size              Stretch
Angularity        Irregularity

STRUCTURE
Relief            Fold
Crease            Push

VARIATION
Detail            Distortion
Variation         Seed

ADVANCED
Size Variation
Smoothness
Distortion Frequency
Mid Operation
Detail Operation
Fold Width
Crease Width
Push Width
Invert
Roughness
Placement Mask
```

Important meanings:

### Relief

Signed.

```text
negative = carve / torn / recessed
zero     = edge-only
positive = raise / rock / plate
```

Suggested range:

```text
-0.5 .. +0.5
```

### Fold

Raises the positive/outside side of the SDF near its boundary.

Useful for:

* torn paper
* peeling paint
* curled mud
* ice plates
* flaking rust

### Crease

Cuts a narrow depression exactly along the SDF zero crossing.

Useful for:

* cracks
* plate separation
* torn seams
* rock joints

### Push

Warps the existing height along the SDF gradient.

Signed.

Useful for:

* compressed material
* pushed rock
* torn edges
* bulging
* warped strata

The Push distance is interpreted in reference pixels at 1K so visual scale remains stable between 1K/2K/4K.

### Variation

Stable per-fragment variation derived from generated IDs.

It must not introduce per-pixel noise.

---

# Recommended UI ranges

```text
Scale
1 .. 64
default 6

Detail
0 .. 1
default 0.5

Density
0 .. 1
default 0.72

Size
0.05 .. 0.75
default 0.32

Size Variation
0 .. 0.75
default 0.3125

Stretch
1 .. 2
default 1.6

Angularity
0 .. 1
default 0.72

Irregularity
0 .. 1
default 0.38

Smoothness
0 .. 1
default 0.30

Distortion
0 .. 32
default 5.6

Distortion Frequency
1 .. 16
default 3

Relief
-0.5 .. 0.5
default -0.06

Fold
0 .. 0.5
default 0.025

Fold Width
0.25 .. 128
default 16

Crease
0 .. 0.5
default 0.018

Crease Width
0.25 .. 64
default 1.25

Push
-128 .. 128
default 0

Push Width
1 .. 256
default 24

Variation
0 .. 1
default 0.25

Amount
0 .. 1
default 1

Roughness
-1 .. 1
default 0

Seed
1 .. 9999
default 1
```

---

# Operation menus

Do not expose integer numbers.

UI:

```text
Mid Operation
  Union
  Subtract
  Intersect

Detail Operation
  Union
  Subtract
  Intersect
```

These operate on the procedural SDF families.

---

# Expected looks

Use these as visual tests.

## Torn paper / peeling material

```text
Scale        5–8
Density      ~0.7
Stretch      ~1.6
Angularity   ~0.7
Relief       -0.10
Fold         +0.05
Crease       0.02
Push         4–8
Variation    0.4
```

Expected:

```text
remaining surface
______________/\____
              ||
              || fold
              \____ recessed torn region
               ^
             crease
```

## Cracks

```text
Relief       0
Fold         0
Crease       0.04–0.10
Crease Width 0.5–2
Push         0
```

## Rock foundation

```text
Relief       +0.08 .. +0.20
Fold         0.01–0.04
Crease       0.02–0.06
Push         2–8
```

Use subtract/intersect on Detail for stronger geological breakup.

## Flaking

```text
Scale        12–24
Size         lower
Relief       -0.03
Fold         0.05–0.10
Crease       0.01
Variation    high
```

## Pure SDF push warp

```text
Relief       0
Fold         0
Crease       0
Push         8–32
Push Width   16–64
```

This must visibly distort the incoming height without replacing it.

---

# Tiling

Breakup must remain perfectly tileable.

Test seams on:

```text
left ↔ right
top ↔ bottom
all four corners
```

The distortion field uses integer-period sinusoidal terms.

Cell hashes wrap using the cell count.

The cell position itself must retain the unwrapped neighbor coordinate so cells across the UV border remain geometrically continuous.

Do not clamp UVs.

Use wrap behavior.

---

# Performance

This replacement is expected to be dramatically cheaper than Chipping.

Old path roughly:

```text
min/max reduction
+
N iterative full-resolution chip passes
+
height resolve
+
normal reconciliation
+
roughness
```

New path:

```text
1 field pass
+
1 apply pass
+
existing normal reconciliation
+
optional roughness
```

Important:

* evaluate the three procedural families once per pixel
* do not regenerate the SDF during ApplyCS
* gradient comes from neighboring texels in `BreakupField`
* no iterative passes
* no full-resolution ID texture
* no temporary float4 simulation state
* no CPU readback

---

# Identity behavior

`BreakupAmount == 0` must be an exact rendering identity.

If possible, skip the passes entirely in C++ when:

```cpp
BreakupAmount <= 0.0f
```

Likewise, avoid the roughness pass when:

```cpp
BreakupRoughnessAmount == 0.0f
```

---

# Rename the integration

Examples:

```text
FMixtormatChippingCS
→ FMixtormatBreakupFieldCS
  FMixtormatBreakupApplyCS

AddChippingPasses
→ AddBreakupPasses

PendingChipping
→ PendingBreakup

GetSelectedChipping
→ GetSelectedBreakup

AddChippingToLayer
→ AddBreakupToLayer

BuildChippingControls
→ BuildBreakupControls

Mixtormat.Chipping.*
→ Mixtormat.Breakup.*
```

Do not leave stale user-facing Chipping names.

---

# Existing effect ordering

Keep Breakup in Chipping's current structural position in the effect pipeline unless there is a concrete dependency reason to move it.

Grade should still act over the final structurally modified surface.

Do not make unrelated effect-order changes.

---

# Tests

At minimum:

```text
Breakup can be added to a layer
Chipping no longer appears anywhere in UI
old serialized enum value 4 loads as Breakup
enum name redirect works
Amount 0 = exact identity
Seed deterministic
different seed visibly changes field
perfect tiling
Density 0 behaves safely
Density 1 behaves safely
Scale 1 behaves safely
Union
Subtract
Intersect
Invert
negative Relief
positive Relief
Fold only
Crease only
Push only
combined Relief/Fold/Crease/Push
Variation is piece-stable, not pixel noise
Pattern IDs optional
Pattern IDs affect stable variation if available
scoped masks
layer masks
inverted masks
stacked Breakup effects
Breakup + Erosion
Breakup + Flow Warp
Breakup + Grade
undo/redo
duplicate effect
save/reload
preview vs bake
1K
2K
4K
```

Verify no seam appears at any resolution.

---

# Build requirements

Build:

```text
UE 5.8
Win64
Development
Mixtormat editor target
SM6
```

Confirm both:

```text
FMixtormatBreakupFieldCS
FMixtormatBreakupApplyCS
```

compile without shader warnings/errors.

---

# Deliverables

When finished, report only:

1. changed files
2. removed Chipping code/files
3. new Breakup code/files
4. enum migration/redirect used
5. final public controls/defaults
6. GPU pass count
7. pattern-ID integration status
8. build result
9. test result
10. remaining issues

Do not redesign unrelated systems.

Do not preserve the old Chipping algorithm.

The goal is specifically:

**replace Chipping with a fast, reusable multi-scale SDF Breakup effect.**
