It has been authored against `main`:

Establish one consistent height-output contract across all current Mixtormat generators:

`Strata Carver`, `Cracks`, `Rock Formation`, `Pebbles`, and `Cliff Strata`.

The target contract is:

```text
generator-native field
        ↓
optional zero-preserving normalization
        ↓
signed range -0.5 .. +0.5
        ↓
signed Height Scale
        ↓
generator modules add together from 0
        ↓
generator layer adds signed result to the document height below
```

The work also includes some Cliff Strata shader corrections that were still inconsistent with the Houdini prototype.

## 1. Runtime generator properties

File:

`Source/MixtormatRuntime/Public/MixtormatMaterial.h`

I added output controls to every generator payload.

Strata Carver gets:

```cpp
bool bStrataNormalizeHeight = true;
float StrataHeightScale = 1.0f;
```

Cracks gets:

```cpp
bool bCrackNormalizeHeight = true;
```

and reuses its existing:

```cpp
float CrackHeightScale;
```

Rock Formation gets:

```cpp
bool bRockNormalizeHeight = true;
```

and reuses:

```cpp
float RockHeightScale;
```

Pebbles gets:

```cpp
bool bPebbleNormalizeHeight = true;
```

and reuses:

```cpp
float PebbleHeightScale;
```

Cliff Strata gets:

```cpp
bool bCliffNormalizeHeight = true;
float CliffHeightScale = 1.0f;
```

The existing Crack/Rock/Pebble scale fields were deliberately preserved instead of renamed so existing serialized assets continue to find the same properties.

Their editor ranges were changed to a common signed scale convention:

```text
-4 .. +4
default 1
```

Negative scale therefore inverts a generator's relief.

## 2. Default layer height operation changed to Add

The default inside `FMixtormatHeightBlend` changes from:

```cpp
EMixtormatHeightOp::HeightBlend
```

to:

```cpp
EMixtormatHeightOp::Add
```

This is a broad default change, not generator-only.

The reason was the requested model where layers/generators are additive by default and a separate module can later own more elaborate blending.

Existing serialized objects with an explicitly saved operation should retain their saved value. Newly default-constructed `FMixtormatHeightBlend` objects default to Add.

This is one area the next agent may want to reconsider if Add should only be the default for Generator layers rather than every new height-blend struct.

## 3. Crack authoring database updated

File:

`Config/MixtormatParameterAuthoring.json`

The shipped authoring metadata for:

```text
Generator.CrackHeightScale
```

was previously:

```text
default 2
UI range 0 .. 8
```

It is changed to:

```text
default 1
UI range -4 .. 4
```

This was necessary because the authoring database overrides the literal `UPROPERTY`/inspector values.

No equivalent JSON entries were added for the newly created normalization toggles or the new Strata/Cliff scale properties.

If these are supposed to participate fully in the developer parameter-authoring system, that is a follow-up.

## 4. Common generator render-data contract

File:

`Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h`

`FGeneratorRenderData` gains:

```cpp
bool bNormalizeHeight = true;
float HeightScale = 1.0f;
```

This means all individual generator payloads are flattened at gather time into one common output policy.

The generator shader itself no longer needs to know the final normalization/scale behavior.

## 5. Generator gather changes

File:

`Source/MixtormatShaders/Private/Compositing/MixtormatGeneratorGather.cpp`

Each generator now populates the common render fields:

```cpp
ChildData.Generator.bNormalizeHeight
ChildData.Generator.HeightScale
```

from its own serialized payload.

Mapping is:

```text
Strata:
bStrataNormalizeHeight
StrataHeightScale

Cracks:
bCrackNormalizeHeight
CrackHeightScale

Rock:
bRockNormalizeHeight
RockHeightScale

Pebbles:
bPebbleNormalizeHeight
PebbleHeightScale

Cliff:
bCliffNormalizeHeight
CliffHeightScale
```

The old:

```cpp
ChildData.GeneratorHeightBlend = Generator.HeightBlend;
```

assignment is removed.

So module composition no longer consumes each generator's private `HeightBlend`.

The underlying serialized `FMixtormatGenerator::HeightBlend` field itself is NOT deleted yet. It is simply no longer gathered/used for module composition.

That is intentional for a first migration pass, but it is now effectively legacy/dead state.

## 6. Cache behavior

Normalization and scale are treated as post-processing controls and are excluded from expensive generator field cache keys.

Cracks adds:

```text
bCrackNormalizeHeight
CrackHeightScale
```

to skipped top-level fields.

Rock adds:

```text
bRockNormalizeHeight
RockHeightScale
```

to skipped cache fields. `RockHeightMode` was already excluded.

Pebbles adds:

```text
bPebbleNormalizeHeight
PebbleHeightScale
```

to skipped fields.

Cliff Strata adds:

```text
bCliffNormalizeHeight
CliffHeightScale
```

to its skipped fields.

This means changing Normalize or Scale should reuse the already generated field and only rerun the cheap output conversion.

Strata currently has no equivalent field-cache structure requiring this treatment.

## 7. Generator shader outputs are changed to zero-centred native fields

The generators should no longer emit a fake heightmap centred around `0.5`.

They emit native signed/zero-neutral values first.

### Strata Carver

File:

`Shaders/Private/MixtormatStrataCarver.usf`

Changed from:

```hlsl
OutHeight[P] =
    0.5f +
    (Strata - 0.5f) * Depth * Amount;
```

to:

```hlsl
OutHeight[P] =
    (Strata - 0.5f) * Depth * Amount;
```

So flat/no contribution is now around `0`, not `0.5`.

### Cracks

File:

`Shaders/Private/MixtormatCracks.usf`

Changed final resolve from:

```hlsl
0.5 + CrackHeight * HeightScale
```

to:

```hlsl
CrackHeight
```

The scale is no longer baked by the Crack shader. It is handled centrally afterward.

### Pebbles

File:

`Shaders/Private/MixtormatPebbles.usf`

Changed:

```hlsl
PebbleHeight * HeightScale
```

to:

```hlsl
PebbleHeight
```

Again, common post-processing owns scale.

### Rock Formation

File:

`Shaders/Private/MixtormatRockFormation.usf`

Changed:

```hlsl
h * HeightScale
```

to:

```hlsl
h
```

The Rock shader resolves its native raw field only.

### Cliff Strata

Cliff already produces a native scalar field rather than a `0.5`-centred layer height, so its final output remains native and is passed through the same common signed conversion afterward.

## 8. Shared zero-preserving normalization

File:

`Shaders/Private/MixtormatFieldRange.usf`

I extended the existing field-range shader rather than creating another normalization shader.

It gains:

```hlsl
uint NormalizeMode;
float OutputScale;
```

There are now three effective modes.

Mode `0` preserves the existing field-range behavior:

```text
measured min/max → OutLow..OutHigh
```

This remains available for existing callers such as old field normalization utilities.

Mode `1` is the new generator normalization:

```hlsl
MaxAbs = max(abs(Min), abs(Max));

Result =
    0.5 * Value / MaxAbs;
```

then clamped to:

```text
-0.5 .. +0.5
```

and multiplied by:

```hlsl
OutputScale
```

This is deliberately not ordinary min/max normalization.

Example:

```text
field range = -0.2 .. +0.8

MaxAbs = 0.8

-0.2 -> -0.125
 0.0 ->  0.0
+0.8 -> +0.5
```

Zero stays exactly zero.

This is important for additive generators because empty/background pixels must not acquire a height offset.

Mode `2`, used when `Normalize Height` is disabled, currently does:

```hlsl
clamp(Value, -0.5, +0.5) * OutputScale
```

So disabling Normalize does NOT mean completely unrestricted raw output.

It means:

```text
do not stretch the field to fill ±0.5
but still enforce the signed generator contract ±0.5
```

This is a likely tweak point if the desired behavior is instead:

```text
Normalize ON:
measured max-abs → ±0.5

Normalize OFF:
raw field * Scale, unclamped
```

The current chose the stricter interpretation that ALL generator outputs must remain `-0.5..+0.5`.

Also note that Scale is applied after the clamp/normalization, so a scale above `1` can make the final result exceed `±0.5`.

Example:

```text
normalized field = +0.5
scale = 2
final = +1.0
```

FIX: Needs to actually do -1 to 1

I intentionally left scale free because the request also asked for controllable intensity.

This semantic point should be decided explicitly.

## 9. New common GPU post-process

File:

`Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp`

Added:

```cpp
AddSignedGeneratorHeightPasses(...)
```

It performs two RDG passes.

First pass:

```text
reduce the generator field to min/max
```

using the existing `FMixtormatFieldRangeCS`.

Second pass:

```text
normalize/clamp
apply output scale
write PF_R32_FLOAT signed height
```

Every generator module now goes through:

```cpp
Module.Height = AddSignedGeneratorHeightPasses(
    ...,
    Child.Generator.bNormalizeHeight,
    Child.Generator.HeightScale,
    ...);
```

before it is added into the Generator-layer module chain.

This centralizes the contract in one place instead of implementing slightly different normalization in five generator shaders.

## 10. Rock private height-mode behavior is bypassed for final generator height

Rock previously had:

```text
Raw
Analytic
Measured
```

height modes and could run its own measured 0..1 normalization.

For the actual generator-module height output, the patch forces:

```cpp
EMixtormatRockHeightMode::Raw
```

with internal shader scale:

```cpp
1.0f
```

Then the shared generator signed-normalization pass handles normalization and scale.

The old `RockHeightMode` property remains serialized in the runtime payload but is no longer used for the final generator module output.

The existing normalized Rock masks/gates such as `RockHeight` published masks are not removed.

The next agent may want to decide whether `EMixtormatRockHeightMode` should be deleted entirely, retained only for published masks, or migrated into the common output system.

## 11. Generator modules no longer have individual blend operations

File:

`Shaders/Private/MixtormatGeneratorBundle.usf`

The old module combine logic used:

```text
Replace
Add
Subtract
Multiply
Min
Max
Difference
Height Blend
Blend Amount
softness
height-blend biases
```

That logic is removed from the module-combine stage.

Stage 9 now simply does:

```hlsl
const float Running =
    RunningHeight.Load(...);

const float Module =
    ModuleHeight.Load(...);

OutScalar[Pixel] =
    Running + Module;
```

So generator modules are now a simple ordered sum of signed fields.

This directly implements the idea that more sophisticated blending will become a separate module rather than a property of every generator.

The C++ `FMixtormatGeneratorBundleCS::FParameters` still contains the old blend parameters:

```text
HeightOp
HeightSoftness
BlendAmount
HbStrength
HbThreshold
HbEdgeSoftness
HbBaseBias
HbBlendBias
```

The patch stops filling them for the module-combine pass, but does NOT delete them from the shader parameter struct or USF declarations.

They are now dead/legacy for `BUNDLE_STAGE == 9`.

This is another cleanup candidate.

## 12. Generator module accumulation now starts at signed zero

Previously:

```cpp
FRDGTextureRef RunningHeight =
    LayerCtx.LayerInputHeight;
```

That meant a Generator layer's module chain began with the existing `0.5`-centred layer/source height.

The patch instead creates:

```text
Mixtormat.Generator.SignedRunningHeight
```

as a black/zero `PF_R32_FLOAT` texture.

So:

```text
module 1 signed height
+
module 2 signed height
+
module 3 signed height
...
```

builds a pure generator delta field.

This is important because `0` now means no generator contribution.

## 13. Generator layer composition into the material stack

File:

`Shaders/Private/MixtormatComposite.usf`

Generator layer height is treated differently from ordinary surface height.

In `SampleIncomingHeight`, when:

```hlsl
IsGenerator != 0
```

the shader now returns the generator field directly.

It skips the normal material-height logic that assumes height is centred around `0.5`, including:

```text
invert around 1
HeightShape
HeightBoost about 0.5
HeightLevelOffset
legacy 0..1 shaping
```

This keeps generator height genuinely signed.

Then `ApplyHeightOp` has generator-specific Add/Subtract handling:

```hlsl
if (IsGenerator)
{
    if (HeightOp == ADD)
        return Below + Layer;

    if (HeightOp == SUBTRACT)
        return Below - Layer;
}
```

This is deliberately different from the ordinary material Add implementation:

```hlsl
Below + (Layer - 0.5)
```

because a Generator layer's neutral point is `0`, not `0.5`.

All non-generator layers continue using the existing `MixtormatApplyHeightOp(...)` midpoint convention.

So the two domains are now:

```text
normal material/surface heights:
neutral midpoint = 0.5

generator module heights:
neutral delta = 0.0
```

## 14. Generator per-type Height Blend UI removed

File:

`Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp`

Calls to:

```cpp
AddGeneratorBlendRows(Panel);
```

were removed from:

```text
Strata Carver
Cracks
Pebbles
Rock Formation
Cliff Strata
```

So individual generator modules no longer show their own blend-operation cards.

The function:

```cpp
SMixtormat::AddGeneratorBlendRows(...)
```

itself remains in the codebase.

It is just no longer called by these generator panels.

The runtime:

```cpp
FMixtormatGenerator::HeightBlend
```

also remains.

These can be intentionally kept for asset compatibility or removed later after checking serialization/migration requirements.

## 15. Common OUTPUT UI added to every generator

Each generator now receives an `OUTPUT` card near the top of its inspector.

Every OUTPUT card contains:

```text
Normalize Height
Scale
```

Normalize defaults on.

Scale defaults to:

```text
1.0
```

and uses:

```text
-4 .. +4
```

### Strata

Uses:

```cpp
bStrataNormalizeHeight
StrataHeightScale
```

### Cracks

Uses:

```cpp
bCrackNormalizeHeight
CrackHeightScale
```

### Pebbles

Uses:

```cpp
bPebbleNormalizeHeight
PebbleHeightScale
```

### Rock

Uses:

```cpp
bRockNormalizeHeight
RockHeightScale
```

### Cliff

Uses:

```cpp
bCliffNormalizeHeight
CliffHeightScale
```

Rock's old inspector `HEIGHT` card containing:

```text
Mode
Scale
```

is removed.

Cracks' old standalone Height Scale card is removed.

Pebbles' old Height Scale control is removed from its original pair and moved to OUTPUT.

Strata and Cliff did not previously have equivalent common output scale controls, so they receive new properties.

## 16. Cliff Strata fixes included in this patch

The patch also modifies:

`Shaders/Private/MixtormatCliffStrata.usf`

because the latest implementation still had known prototype-port problems.

### Horizontal Shape sweep

The first horizontal eikonal sweep had accidentally been changed to read:

```hlsl
SweepIn
```

as if it were the Voronoi modulation.

The patch restores:

```hlsl
VoronoiSmooth
```

for:

```text
v
signed Voronoi modulation
CarveVoronoi
CarveDepth modulation
```

The height itself remains sourced from `RawHeight`.

The reverse horizontal sweep retains the depth-feedback logic:

```text
depth = abs(current - raw)
modulation = depth / unit distance
```

which is the important behavior that makes `CarveDepth` actually affect propagation.

### Vertical Shape sweep

The entire stage 4 implementation is replaced.

The replacement uses:

```text
TOP → BOTTOM
BOTTOM → TOP
```

directly in row order rather than the prior UE-row inversion mapping.

It preserves:

```text
depth = abs(horizontalHeight - RawHeight)
ID unit-distance variation
YBias
YBiasVoronoi
YBiasVoronoiInvert
CarveVoronoi
CarveDepth
NegativeYUnitDistanceTaper
Reverse/dir behavior
```

This was meant to move the UE stage closer to the literal OpenCL Shape iteration 3 instead of adapting it through a texture-origin reinterpretation.

This is something the next agent should compare directly against:

`AgentDocs/Prototypes/CliffStrata/CliffStrata_shape.cl`

before changing it further.

### Cliff final cavity output

Previously:

```hlsl
OutCavity[p] = 1.0;
```

which was effectively a bogus constant output.

The patch computes:

```hlsl
cavity = max(blockCavity, rowCavity);
```

and writes:

```hlsl
OutCavity[p] = cavity;
```

The final height still subtracts:

```hlsl
CavityIntensity * cavity
```

The existing corrected Voronoi seam convention is retained:

```hlsl
seam = 1 - VoronoiRaw
```

with:

```text
ChamferVoronoi
CavityVoronoiThreshold
CavityVoronoiMaskGain
```

operating through that seam.

## 17. What this patch does NOT remove

The migration is intentionally not a full cleanup.

The following legacy concepts may remain after applying it:

```text
FMixtormatGenerator::HeightBlend
FChildRenderData::GeneratorHeightBlend
AddGeneratorBlendRows(...)
GeneratorBundle HeightOp/Hb* shader parameters
RockHeightMode runtime property
some generator-local HeightScale shader parameters
comments referring to the previous midpoint/blend behavior
parameter-authoring metadata for only some of the new controls
```

Some are no longer functionally used.

The next agent should not assume that their continued presence means the new system still depends on them.

## 18. Important behavioral decisions worth reviewing

The most important thing to decide is exactly what `Normalize Height OFF` and `Scale` are supposed to mean.

Current patch semantics:

```text
Normalize ON:
raw signed field
→ max-abs normalize
→ clamp -0.5..0.5
→ Scale

Normalize OFF:
raw signed field
→ clamp -0.5..0.5
→ Scale
```

That means `Scale > 1` can exceed `±0.5`.

Alternative A would be:

```text
Normalize OFF:
raw field → Scale
```

with no clamp.

Alternative B would enforce the contract after scale:

```text
normalize/clamp
→ Scale
→ final clamp -0.5..0.5
```

The original request was both:

```text
all generators must produce signed -0.5..0.5
```

and:

```text
have options to normalize and scale their amounts
```

so this interaction needs an explicit product decision.

## 19. Another point to verify: normalization cost

Every generator module currently runs a min/max reduction when it passes through:

```cpp
AddSignedGeneratorHeightPasses(...)
```

Even if Normalize Height is disabled, the current helper still performs the reduction, although mode 2 does not actually need the measured range.

That can be optimized:

```text
Normalize ON:
reduce + normalize pass

Normalize OFF:
skip reduce
run only clamp/scale pass
```

For many generator modules at 4K this is worth cleaning up.

## 20. Another point to verify: flow ordering

The current normalizes/scales:

```text
generator native field
→ signed normalize/scale
→ Generator Flow tools
→ publish/combine
```

because `AddSignedGeneratorHeightPasses` was inserted before:

```cpp
HasActiveFlowTools(...)
```

If flow tools expect the generator's original native amplitude or use height as a deformation input, the better order may be:

```text
native generator field
→ flow/deformation
→ signed normalize/scale
→ publish/combine
```

This should be checked against Rock/Pebble/Crack flow semantics before considering the migration final.

Cliff Strata is not supposed to own Generator Flow according to the earlier design, so this mostly concerns the eligible generators.

## 21. Another point to verify: generator normals

The Generator layer's final field is now signed around `0`, not a complete absolute layer height.

The pipeline still calls the generator-layer normal reconciliation around:

```cpp
AddHeightDerivedNormalPass(
    SourceHeight,
    LayerCtx.LayerInputHeight,
    ...
)
```

The next agent should verify that this call is now comparing compatible domains.

If `SourceHeight` is an absolute document height around `0.5` while `LayerInputHeight` is a signed generator delta around `0`, then the normal-delta calculation may need to instead see:

```text
absoluteAfter = SourceHeight + GeneratorDelta
```

or derive normals directly from the signed delta.

This is one of the highest-priority runtime checks.

## 22. Files the latest touches

| File | Purpose |
|---|---|
| `Source/MixtormatRuntime/Public/MixtormatMaterial.h` | output properties, Add default |
| `Config/MixtormatParameterAuthoring.json` | Crack scale authoring metadata |
| `Source/MixtormatShaders/Private/MixtormatGpuCompositorInternal.h` | common generator output render data |
| `Source/MixtormatShaders/Private/Compositing/MixtormatGeneratorGather.cpp` | gather normalize/scale and cache handling |
| `Shaders/Private/MixtormatStrataCarver.usf` | zero-centred native height |
| `Shaders/Private/MixtormatCracks.usf` | remove local midpoint/scale |
| `Shaders/Private/MixtormatPebbles.usf` | remove local scale |
| `Shaders/Private/MixtormatRockFormation.usf` | remove local final scale |
| `Shaders/Private/MixtormatCliffStrata.usf` | prototype fixes + actual cavity output |
| `Shaders/Private/MixtormatFieldRange.usf` | shared signed normalization modes |
| `Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp` | signed post-process and zero-based module chain |
| `Shaders/Private/MixtormatGeneratorBundle.usf` | module composition becomes direct addition |
| `Shaders/Private/MixtormatComposite.usf` | generator-specific signed Add/Subtract |
| `Source/MixtormatEditor/Private/Widgets/Inspector/MixtormatInspectorGenerators.cpp` | common OUTPUT UI, remove per-generator blend cards |

## 23. What needs inpsection.

- Verify the generator normal derivation now that `LayerInputHeight` is a signed delta rather than an absolute `0.5`-centred height.
- Decide whether normalization occurs before or after Generator Flow tools.
- Decide exact semantics of Normalize OFF and whether Scale may exceed the nominal `±0.5` contract.
- Optimize Normalize OFF so it does not run an unnecessary GPU reduction.
- Verify the Cliff Strata vertical row orientation directly against `CliffStrata_shape.cl`; do not infer it from comments.
- Audit all Cliff Voronoi consumers again: `CarveVoronoi`, `FlowVoronoi`, `YBiasVoronoi`, `ChamferVoronoi`, cavity threshold/gain.
- Decide whether to delete or retain the now-unused per-generator `HeightBlend`, bundle `HeightOp/Hb*` plumbing, and `RockHeightMode`.
- Add proper parameter-authoring metadata for the new Normalize/Scale controls if the persistent authoring database is supposed to own all UI defaults/ranges.
- Check asset migration/default behavior because changing `FMixtormatHeightBlend::Op` to Add affects newly default-constructed non-generator layers too.