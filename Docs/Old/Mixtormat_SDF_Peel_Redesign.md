# Mixtormat Peel System — SDF-Based Redesign

## Goal

Replace the current **seed → eikonal propagation → arrival-time resolve** peel system with a deterministic **mask contour → signed distance field → peel shaping** pipeline.

The new system should:

- Avoid randomized or threshold-based seeding.
- Treat the input mask as the authored peel shape.
- Produce a true signed distance field.
- Remain tileable.
- Be efficient at 1K–4K resolutions.
- Preserve surface-driven controls such as curvature, AO, height, macro warp, micro warp, peel width, lift, and edge sharpness.
- Remove the need for iterative eikonal growth and arrival-time storage.

---

## Current Architecture

Current flow:

```text
Mask
  ↓
Seed Threshold
  ↓
Arrival = 0 at seed pixels
  ↓
Iterative Eikonal Solve
  ↓
Arrival Field
  ↓
Resolve
  ↓
Coverage / Edge / Detail / Height
```

The mask currently determines where propagation begins. Surface properties then affect the speed of the advancing front.

Main consequences:

- Shape depends on propagation history.
- Large textures can require many solve iterations.
- `Arrival` is not a true geometric signed distance.
- Front controls are less intuitive.
- Surface influence affects travel speed rather than directly modifying the peel boundary.

---

# Proposed Architecture

```text
Mask / Surface Field
        ↓
   Iso-Contour
        ↓
 Boundary Detection
        ↓
 Jump Flood SDF
        ↓
 Signed Distance
        ↓
 Surface / Warp Displacement
        ↓
 Peel Coverage
        ↓
 Edge / Detail / Height
```

Recommended passes:

```cpp
Mode 0 = BuildBoundary
Mode 1 = JumpFlood
Mode 2 = Resolve
```

---

# 1. Boundary Detection

The input mask defines a scalar field.

A configurable threshold defines the peel contour:

```hlsl
float F = Mask - SDFThreshold;
```

Sign convention:

```text
SDF < 0    inside peel region
SDF = 0    peel boundary
SDF > 0    outside peel region
```

Detect pixels adjacent to the zero crossing:

```hlsl
float Mask = PeelMask(UV);
float F = Mask - SDFThreshold;

bool Inside = F >= 0.0f;

float L = PeelMask(UV - float2(Texel.x, 0.0f)) - SDFThreshold;
float R = PeelMask(UV + float2(Texel.x, 0.0f)) - SDFThreshold;
float D = PeelMask(UV - float2(0.0f, Texel.y)) - SDFThreshold;
float U = PeelMask(UV + float2(0.0f, Texel.y)) - SDFThreshold;

bool Boundary =
    (F * L <= 0.0f) ||
    (F * R <= 0.0f) ||
    (F * D <= 0.0f) ||
    (F * U <= 0.0f);
```

Each boundary texel stores its own UV as the initial nearest-boundary candidate.

Example packed representation:

```hlsl
float4(
    BoundaryUV.x,
    BoundaryUV.y,
    Inside ? -1.0f : 1.0f,
    Boundary ? 1.0f : 0.0f
);
```

---

# 2. Jump Flood SDF

Use Jump Flooding instead of iterative eikonal propagation.

For a 2048×2048 texture:

```text
log2(2048) ≈ 11 passes
```

Typical jump sequence:

```text
1024
512
256
128
64
32
16
8
4
2
1
```

Each pass compares the current nearest-boundary candidate against candidates found at the current jump distance.

Neighbor pattern:

```hlsl
int2 Offsets[8] =
{
    int2(-1,-1), int2(0,-1), int2(1,-1),
    int2(-1, 0),             int2(1, 0),
    int2(-1, 1), int2(0, 1), int2(1, 1)
};
```

---

# 3. Tileable Distance

Distance comparisons must wrap across UV borders.

Do not use direct Euclidean UV subtraction.

Use:

```hlsl
float2 Delta = abs(CandidateUV - UV);
Delta = min(Delta, 1.0f - Delta);
```

Convert to texel-space distance:

```hlsl
float2 Metric = Delta * float2(Resolution);
float Dist2 = dot(Metric, Metric);
```

This makes the nearest-boundary search periodic and prevents seams across texture borders.

---

# 4. Final Signed Distance

After the final Jump Flood pass:

```hlsl
float2 Delta = abs(NearestUV - UV);
Delta = min(Delta, 1.0f - Delta);

float DistancePx = length(Delta * float2(OutputSize));

float SDF = Sign * DistancePx;
```

Recommended units:

```text
pixels
```

This makes controls such as `Front` and `Width` resolution-intuitive.

Example:

```text
Front = 0       original mask contour
Front = +10     expand peel approximately 10 px
Front = -10     contract peel approximately 10 px
```

---

# 5. Surface-Driven SDF Displacement

Curvature, AO, height, and other surface fields should modify the SDF directly.

Example:

```hlsl
float Surface =
      Curvature * CurvatureWeight
    + AO        * AOWeight
    + Height    * HeightWeight;

float SurfaceDisplacement =
    (Surface - 0.5f) * GrowthStrength;
```

Final peel distance:

```hlsl
float D =
    SDF
    + SurfaceDisplacement
    + MacroDisplacement
    + MicroDisplacement
    - Front;
```

Then normalize by peel width:

```hlsl
float X = D / max(Width, 1.0e-5f);
```

This is preferable to modifying propagation velocity because surface features now directly deform the peel contour.

---

# 6. Peel Coverage

Basic peel coverage:

```hlsl
float Coverage =
    1.0f - Smooth01(X * 0.5f + 0.5f);
```

The zero-crossing of `D` is the peel edge.

`Width` controls the transition thickness.

---

# 7. Edge Field

The existing Gaussian-style edge profile can remain:

```hlsl
float Edge =
    pow(
        exp(-2.0f * X * X),
        max(EdgeSharpness, 0.01f)
    );
```

This provides a concentrated signal around the peel boundary.

Useful for:

- raised paint edge
- curl
- chipping
- material thickness
- roughness variation
- normal intensity
- accumulated dirt

---

# 8. SDF Gradient

A major benefit of having a true SDF is that the boundary normal can be derived directly.

Central difference:

```hlsl
float Sx0 = SDF(Pixel + int2(-1,  0));
float Sx1 = SDF(Pixel + int2( 1,  0));
float Sy0 = SDF(Pixel + int2( 0, -1));
float Sy1 = SDF(Pixel + int2( 0,  1));

float2 Gradient =
    normalize(float2(
        Sx1 - Sx0,
        Sy1 - Sy0
    ));
```

The gradient points perpendicular to the peel boundary.

Possible uses:

```text
curl direction
flake lifting
edge displacement
edge normals
anisotropic breakup
material separation
directional chips
peel flap orientation
```

---

# 9. Macro and Micro Warp

Instead of modifying arrival time:

```hlsl
D = T + Macro + Micro - Front;
```

modify the SDF:

```hlsl
D =
    SDF
    + MacroWarpField * MacroWarp
    + MicroWarpField * MicroWarp
    + SurfaceDisplacement
    - Front;
```

The displacement fields should preferably be spatially coherent rather than pure per-pixel noise.

Good sources include:

- blurred mask differences
- curvature
- height gradients
- warped low-frequency texture fields
- cellular regions
- material-specific masks

---

# 10. Flake Variation

Randomized seeding is no longer required.

If flake variation is still desired, use region-based variation only for secondary properties.

Example uses:

```text
lift amount
thickness
edge curl
roughness
flake breakage
detail intensity
```

Do not use it to determine where peeling starts.

Example:

```hlsl
float FlakeLift =
    Lift * lerp(
        1.0f,
        FlakeRandom,
        saturate(LiftVariation)
    );
```

---

# 11. Height Construction

For a simple peel mode:

```hlsl
float Intact = 1.0f - Coverage;

float H =
    Intact * Thickness
    + Edge * FlakeLift
    + Detail * DetailStrength;
```

The peeled region should fall back toward the substrate.

The edge may rise above the intact coating when `Lift > 0`.

---

# 12. Curled Peel Variant

The current curled-flap concept can remain.

Use `X`, derived from the SDF, instead of arrival time.

Example:

```hlsl
float Intact = Smooth01(X * 0.5f + 0.5f);

float Fx = (X + 0.80f) / 0.55f;
float Flap = exp(-2.0f * Fx * Fx);

float Bx = (X - 0.75f) / 0.65f;
float Fold = exp(-2.0f * Bx * Bx);
```

Then:

```hlsl
Coverage = 1.0f - Intact;

H =
    Intact * Thickness
    + Edge * FlakeLift
    + Flap * FlakeLift * 0.45f
    - Fold * FlakeLift * 0.15f;
```

---

# 13. Recommended Internal Outputs

The new solver should expose or internally retain:

```text
SDF
SDF Gradient XY
Coverage
Edge
Detail
Height
```

Optional:

```text
Nearest Boundary UV
Surface Displacement
Macro Displacement
Micro Displacement
Flake Region ID
```

---

# 14. Data That Can Be Removed

The SDF architecture eliminates the need for:

```text
Arrival
PreviousArrival
Eikonal2()
Solve8()
Slowness
Propagation speed
Seed flag storage
Hundreds of iterative solve passes
```

The existing eikonal solver can therefore be completely removed once the new path is validated.

---

# 15. Parameters to Preserve

Existing artistic controls can remain conceptually intact:

```text
MaskWeight
PeelMaskTiling
PeelMaskInvert
UseOwnMask

CurvatureWeight
CurvatureBias
AOWeight
HeightWeight
NormalizeWeights
GrowthStrength

PeelType
Front
Width
MacroWarp
MicroWarp
MicroMorph
Thickness
Lift
DetailStrength
LiftVariation
EdgeSharpness
```

Their implementation changes, but their artistic intent remains useful.

`Seed`, `SeedThreshold`, and other propagation-specific parameters should only be removed after confirming compatibility with existing Mixtormat assets and UI.

---

# 16. Recommended GPU Resources

Conceptual resource layout:

```hlsl
Texture2D<float4> SurfaceNormal;
Texture2D<float4> SurfaceRAM;
Texture2D<float>  SurfaceHeight;
Texture2D<float>  ChildMask;
Texture2D<float4> PeelOwnMask;

Texture2D<float4> PreviousNearest;

RWTexture2D<float4> OutputNearest;
RWTexture2D<float4> OutputFieldA;
RWTexture2D<float4> OutputFieldB;
```

Possible `OutputNearest` packing:

```text
R = nearest boundary U
G = nearest boundary V
B = sign
A = valid
```

---

# 17. Suggested Resolve Packing

Example:

```text
Field A
R = SDF
G = Gradient X
B = Gradient Y
A = Surface displacement

Field B
R = Coverage
G = Edge
B = Detail
A = Height
```

This is preferable to storing arrival time once the eikonal path is removed.

---

# 18. Performance

For a square texture of side `N`:

Eikonal-style iterative propagation may require a number of passes proportional to the distance the front must travel.

Jump Flooding requires approximately:

```text
ceil(log2(N))
```

Examples:

```text
512     → ~9 passes
1024    → ~10 passes
2048    → ~11 passes
4096    → ~12 passes
```

The JFA may still run at reduced resolution if desired, but full-resolution SDF generation becomes much more practical than the current repeated front solve.

---

# 19. Migration Plan

Recommended migration sequence:

```text
1. Add BuildBoundary pass.
2. Add nearest-boundary texture.
3. Implement wrapped JFA.
4. Generate signed pixel-space SDF.
5. Replace Arrival with SDF in Resolve.
6. Move curvature/AO/height influence into SDF displacement.
7. Derive Coverage and Edge from SDF.
8. Add SDF gradient.
9. Port curled peel mode.
10. Validate tile wrapping.
11. Compare output against current authored peel maps.
12. Remove eikonal and arrival resources after validation.
```

---

# Final Architecture

```text
Peel Mask
   │
   ├── threshold / iso-value
   │
   ▼
Boundary Detection
   │
   ▼
Wrapped Jump Flood
   │
   ▼
Signed Distance Field
   │
   ├── curvature
   ├── AO
   ├── height
   ├── macro displacement
   └── micro displacement
   │
   ▼
Deformed SDF
   │
   ├── Front
   └── Width
   │
   ▼
Coverage
   │
   ├── Edge
   ├── Gradient
   ├── Detail
   └── Height / Curl
   │
   ▼
Mixtormat Peel Outputs
```

## Recommended Direction

Use a **wrapped Jump-Flood signed distance field with surface-driven SDF displacement** as the primary peel representation.

The mask defines the peel shape.

The SDF defines distance from its edge.

Surface data deforms that distance.

The final zero-crossing defines the peel boundary.

This removes seeding and arrival-time propagation while giving Mixtormat a cleaner, faster, and more controllable peel model.
