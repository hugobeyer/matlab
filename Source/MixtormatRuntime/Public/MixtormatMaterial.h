#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UObject/SoftObjectPtr.h"
#include "MixtormatEffect.h"
#include "MixtormatMaskShaping.h"
#include "MixtormatMaterial.generated.h"

class UMaterialInterface;
class UMixtormatEffect;
class UMixtormatMask;
class UMixtormatSurface;
class UTexture2D;

UENUM(BlueprintType)
enum class EMixtormatLayerType : uint8
{
	Material UMETA(DisplayName = "Material Layer"),
	Fill UMETA(DisplayName = "Fill Layer"),
	Effect UMETA(DisplayName = "Effect Layer")
};

UENUM(BlueprintType)
enum class EMixtormatCompositionMode : uint8
{
	Replace UMETA(DisplayName = "Replace"),
	Coat UMETA(DisplayName = "Coat")
};

UENUM(BlueprintType)
enum class EMixtormatLayerChannelMode : uint8
{
	CompleteSurface UMETA(DisplayName = "Complete Surface"),
	NormalDetail UMETA(DisplayName = "Normal Detail")
};

UENUM(BlueprintType)
enum class EMixtormatNormalSourceType : uint8
{
	Surface UMETA(DisplayName = "Surface Normal"),
	Texture UMETA(DisplayName = "Standalone Normal")
};

UENUM(BlueprintType)
enum class EMixtormatNormalBlendMode : uint8
{
	Combine UMETA(DisplayName = "Combine (RNM)"),
	Override UMETA(DisplayName = "Override")
};

UENUM(BlueprintType)
enum class EMixtormatHeightSource : uint8
{
	Automatic = 0 UMETA(DisplayName = "Automatic (Legacy)"),
	RAMHAlpha = 1 UMETA(DisplayName = "RAMH Height"),
	CombinedMask = 2 UMETA(DisplayName = "Mask as Height"),
	Constant = 3 UMETA(DisplayName = "Constant Height"),
	LayerHeight = 4 UMETA(DisplayName = "Layer Height")
};

UENUM(BlueprintType)
enum class EMixtormatMaskBlendMode : uint8
{
	Replace UMETA(DisplayName = "Replace"),
	Add UMETA(DisplayName = "Add"),
	Subtract UMETA(DisplayName = "Subtract"),
	Multiply UMETA(DisplayName = "Multiply"),
	Min UMETA(DisplayName = "Min"),
	Max UMETA(DisplayName = "Max"),
	AddSub UMETA(DisplayName = "Add/Sub"),
	Overlay UMETA(DisplayName = "Overlay")
};

// Quarter turns only. The compositor wraps every source read in a frac(), so a transform has
// to map the unit square onto itself or it seams at the repeat; an arbitrary angle drags the
// corners of the tile outside the domain. Same reason per-axis scale is an integer.
UENUM(BlueprintType)
enum class EMixtormatUVRotation : uint8
{
	None UMETA(DisplayName = "0"),
	Quarter UMETA(DisplayName = "90"),
	Half UMETA(DisplayName = "180"),
	ThreeQuarter UMETA(DisplayName = "270")
};

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatMaskLayer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	TSoftObjectPtr<UMixtormatMask> Mask;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	TSoftObjectPtr<UTexture2D> MaskTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::Replace;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Weight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	FMixtormatMaskShaping Shaping;

	// Source placement. Integer per axis, because a fractional scale lands mid-cell at the UV
	// wrap and seams.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask", meta = (ClampMin = "1"))
	int32 TilingX = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask", meta = (ClampMin = "1"))
	int32 TilingY = 1;

	// In UV, and unrelated to Offset below, which lifts the mask value rather than moving
	// where it is read from. Safe at any value: translating a periodic function leaves it
	// periodic.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	float UVOffsetX = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	float UVOffsetY = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	bool bFlipU = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	bool bFlipV = false;

	// Applied before the tiling, so the mask turns and the lattice replicates the turned
	// result. Rotating afterwards would turn each cell instead of the pattern.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	EMixtormatUVRotation Rotation = EMixtormatUVRotation::None;

};

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatGeneratedMask
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask")
	bool bEnabled = true;

	// Signal weights. All default to zero so a new node is neutral until authored.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Signals", meta = (ClampMin = "-8.0", ClampMax = "8.0"))
	float CurvatureWeight = 0.0f;

	// 0 = cavity, 1 = convex.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Signals", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CurvatureBias = 0.0f;

	// Raw curvature is a normal difference over 2 x radius, so it is small. Strength is gain.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Signals", meta = (ClampMin = "0.0"))
	float CurvatureStrength = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Signals", meta = (ClampMin = "0.001"))
	float CurvaturePower = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Signals", meta = (ClampMin = "-8.0", ClampMax = "8.0"))
	float DirectionWeight = 0.0f;

	// Tangent-space direction in degrees. 90 is +Y.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Signals", meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float DirectionAngle = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Signals", meta = (ClampMin = "0.001", ClampMax = "64.0"))
	float DirectionBroadness = 1.0f;

	// Positive weight uses inverted AO, concentrating in occluded areas.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Signals", meta = (ClampMin = "-8.0", ClampMax = "8.0"))
	float AOWeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Signals", meta = (ClampMin = "-8.0", ClampMax = "8.0"))
	float HeightWeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Signals", meta = (ClampMin = "-8.0", ClampMax = "8.0"))
	float HeightBias = 0.0f;

	// Drainage and crest lines from an erosion filter on a layer below. Unlike the other
	// signals this is not derived here: erosion already computes where its passes agree on a
	// crest, and this is the first thing to consume that output. Zero everywhere when nothing
	// below erodes, so a weight on it is inert rather than wrong.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Signals", meta = (ClampMin = "-8.0", ClampMax = "8.0"))
	float RidgeWeight = 0.0f;

	// Shaping.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Shaping")
	bool bNormalizeWeights = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Shaping", meta = (ClampMin = "1", ClampMax = "32"))
	int32 Broadness = 2;

	// Averages curvature over this many widening rings. 1 is a single kernel.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Shaping", meta = (ClampMin = "1", ClampMax = "4"))
	int32 Smoothing = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Shaping", meta = (ClampMin = "0.001", ClampMax = "0.999"))
	float Bias = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Shaping", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float WarpAmount = 0.0f;

	// Flow source: 0 samples the accumulated normal slope, 1 the accumulated height
	// gradient. Both point downhill, so intermediate values blend two flow fields.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Shaping", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WarpSource = 0.0f;

	// Gradient sample radius in pixels. Larger values follow broader slopes.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Shaping", meta = (ClampMin = "1", ClampMax = "16"))
	int32 WarpRadius = 1;

	// Accumulator controls, matching FMixtormatMaskLayer.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Blend")
	EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::Multiply;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Blend", meta = (ClampMin = "0.0", ClampMax = "8.0"))
	float Weight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Blend")
	bool bInvert = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Blend", meta = (ClampMin = "0.0", ClampMax = "16.0"))
	float Balance = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Blend", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float Contrast = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Blend", meta = (ClampMin = "-8.0", ClampMax = "8.0"))
	float Offset = 0.0f;

	bool HasAnySignal() const
	{
		return !FMath::IsNearlyZero(CurvatureWeight)
			|| !FMath::IsNearlyZero(DirectionWeight)
			|| !FMath::IsNearlyZero(AOWeight)
			|| !FMath::IsNearlyZero(HeightWeight)
			|| !FMath::IsNearlyZero(RidgeWeight);
	}
};


// Which curvature the erosion cavity gate measures, all in the raw height-field
// convention where positive is the named feature.
//
// Mean is the trace, the cheapest and the least selective. Valley is the larger principal
// curvature: a channel has one strongly concave direction and one flat, which the trace
// halves and a saddle cancels entirely, so Valley is what finds drainage. Ridge is the
// negated smaller one and finds crests, which is not the same signal inverted.
// Tonemap operator the grade filter applies.
//
// Reinhard never clips but desaturates highlights and only reaches white at infinity, so
// bright areas go pale rather than bright. ACES is Narkowicz's fit: contrastier, with a
// filmic toe, and the closest of the three to what a renderer will do to this surface later.
// Filmic is Hable's Uncharted 2 curve, normalised at its white point.
UENUM(BlueprintType)
enum class EMixtormatGradeTonemap : uint8
{
	None = 0 UMETA(DisplayName = "None"),
	Reinhard = 1 UMETA(DisplayName = "Reinhard"),
	ACES = 2 UMETA(DisplayName = "ACES"),
	Filmic = 3 UMETA(DisplayName = "Filmic")
};

UENUM(BlueprintType)
enum class EMixtormatErosionCurvatureMode : uint8
{
	Mean = 0 UMETA(DisplayName = "Mean"),
	Valley = 1 UMETA(DisplayName = "Valley"),
	Ridge = 2 UMETA(DisplayName = "Ridge")
};

// Both stain looks use the same transport solve. Wet exposes absorbed liquid; Deposit exposes
// the dried dirt/mineral residue left behind by that liquid.
UENUM(BlueprintType)
enum class EMixtormatStainMode : uint8
{
	Wet = 0 UMETA(DisplayName = "Wet"),
	Deposit = 1 UMETA(DisplayName = "Deposit")
};

// Peel edge profile. Flat is the chip the authored maps ship. Curled lifts a flap ahead of
// the front and folds it back behind, and exists only on the procedural path.
UENUM(BlueprintType)
enum class EMixtormatPeelType : uint8
{
	Flat = 0 UMETA(DisplayName = "Flat"),
	Curled = 1 UMETA(DisplayName = "Curled")
};

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatLayerEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	TSoftObjectPtr<UMixtormatEffect> Effect;

	// Procedural effects reference no asset, so the type cannot be read from one.
	// Ignored whenever Effect resolves; asset-backed effects keep taking their type
	// from the asset exactly as before.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect")
	EMixtormatEffectType ProceduralType = EMixtormatEffectType::Peeling;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Strength = 1.0f;

	// Negative erodes inside the mask contour, positive dilates outside it. The procedural
	// field is a signed distance, so both directions are meaningful; the lower bound used to
	// be 0 because the field could only ever dilate. Widening a clamp changes no serialized
	// value, so the authored path still resolves identically for any input it already held.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Front = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling", meta = (ClampMin = "0.000001"))
	float Width = 0.015f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling")
	float MacroWarp = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling")
	float MicroWarp = 0.003f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MicroMorph = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling", meta = (ClampMin = "0.0"))
	float Thickness = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling", meta = (ClampMin = "0.0"))
	float Lift = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling", meta = (ClampMin = "0.0"))
	float DetailStrength = 0.02f;

	// Procedural peeling. Active when the child references no effect asset, matching the
	// way Erosion identifies itself. The peel field is then generated from noise and from
	// the surface composited below instead of an imported PDM/MSK/H/SDF set; Front, Width,
	// Thickness, Lift and Detail Strength above keep their meanings either way.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	EMixtormatPeelType PeelType = EMixtormatPeelType::Flat;

	// Cells across one UV repeat. Integral, so the generated peel tiles.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	int32 PeelMacroPeriod = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	int32 PeelMicroPeriod = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	int32 PeelRandomSeed = 1;

	// How much of the surface begins peeling. The mixed seed signal is thresholded here,
	// then the peel grows outward from whatever survives.
	// The peel's own mask. Independent of the layer's ordered mask children, so a layer
	// can carry masks for its other work and still seed peeling from something else.
	// Falls back to the accumulated child mask when unset, which is what existing recipes
	// were built against.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	TSoftObjectPtr<UMixtormatMask> PeelMask;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	TSoftObjectPtr<UTexture2D> PeelMaskTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	int32 PeelMaskTiling = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	bool bPeelMaskInvert = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	float PeelSeedThreshold = 0.62f;

	// Seed weights. Each signal is read from the surface accumulated below the owning
	// layer, so peeling follows whatever it sits on.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	float PeelSeedNoiseWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	float PeelSeedCurvatureWeight = 0.0f;

	// 0 seeds cavities, 1 seeds convex ridges.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	float PeelSeedCurvatureBias = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	float PeelSeedAOWeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	float PeelSeedHeightWeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	float PeelSeedMaskWeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	bool bPeelNormalizeSeedWeights = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	int32 PeelCurvatureRadius = 2;

	// How strongly the growth signals speed the peel up or slow it down. 0 propagates
	// uniformly and ignores the weights entirely.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	float PeelGrowthStrength = 1.0f;

	// Contact occlusion under the lifted edge. The authored path derives this from the
	// map's encoded height range, which a generated field does not have.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	float PeelAOStrength = 0.8f;

	// Exponent on the crest term. 1 is the authored profile; higher tightens the edge.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	float PeelEdgeSharpness = 1.0f;

	// Spread of lift across flakes. 0 lifts every flake equally; 1 scales each by its own
	// random value, so some sit almost flat and others stand well clear.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	float PeelLiftVariation = 0.6f;

	// Spread of extent across flakes. 0 grows every flake to the same radius.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	float PeelSizeVariation = 0.5f;

	// Clustering. A low-frequency field biases the seed threshold so flakes gather in
	// patches instead of scattering evenly. 0 is a flat threshold everywhere.
	// Cell count for per-flake variation: one random value per cell, so flakes differ from
	// their neighbours rather than dissolving into noise inside themselves.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	int32 PeelClusterPeriod = 4;

	// The eikonal solve dominates cost. Dividing the side both quarters the texels and
	// halves the passes the front needs to cross them, and arrival is smooth enough to
	// filter back up. 1 solves at full composition resolution.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	int32 PeelSolveDivisor = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	float PeelClusterAmount = 0.35f;

	// Domain warp on the seed field. Source 0 is divergence-free curl noise, 1 is the
	// accumulated height gradient.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	int32 PeelWarpPeriod = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	float PeelWarpAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	float PeelWarpSource = 0.0f;

	// How much peel relief reaches the composited height. 0 keeps the old behaviour, where
	// peeling changed coverage and normals but never displaced anything.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling")
	float PeelHeightAmount = 1.0f;

	// Flips the relief: 0 stands the intact film above the substrate, 1 cuts it in.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling")
	bool bPeelHeightInvert = false;

	// Stain solves transport from the accumulated surface and resolves the selected wet or deposit
	// result into the layer's mask chain. It does not author any surface channel directly.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain")
	EMixtormatStainMode StainMode = EMixtormatStainMode::Wet;

	// Optional source mask. Unset uses the accumulated mask children at the stain's position.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Source")
	TSoftObjectPtr<UMixtormatMask> StainSourceMask;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Source")
	TSoftObjectPtr<UTexture2D> StainSourceMaskTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Source")
	int32 StainSourceMaskTiling = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Source")
	bool bStainSourceMaskInvert = false;

	// Optional dirt/mineral map. Unset reuses the source mask, so a single mask remains useful.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Dirt")
	TSoftObjectPtr<UMixtormatMask> StainDirtMask;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Dirt")
	TSoftObjectPtr<UTexture2D> StainDirtMaskTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Dirt")
	int32 StainDirtMaskTiling = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Dirt")
	bool bStainDirtMaskInvert = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Simulation")
	int32 StainIterations = 20;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Simulation")
	int32 StainSeed = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Simulation")
	float StainSourceAmount = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Simulation")
	float StainGravity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Simulation")
	float StainSurfaceFollow = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Simulation")
	float StainSpread = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Simulation")
	float StainAbsorption = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Simulation")
	float StainDrying = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Simulation")
	float StainDirtAmount = 0.35f;

	// Existing surface analysis participates directly in source placement. Positive values add
	// liquid/dirt at concave or convex detail; zero leaves placement entirely mask-driven.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Surface")
	float StainConcavityWeight = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Surface")
	float StainConvexityWeight = 0.15f;

	// Occlusion, height and slope complete the auto source. All four surface weights read the
	// surface accumulated below the layer, so a stain can be driven entirely by geometry with no
	// Liquid Mask at all. Zero by default: curvature alone is the conservative starting point.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Surface")
	float StainOcclusionWeight = 0.0f;

	// Signed. Positive sources runoff from high ground, negative pools liquid in the low.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Surface")
	float StainHeightWeight = 0.0f;

	// SourceHeightBias, not HeightBias: StainHeightBias is taken by the deprecated gather-era
	// control further down, and UHT rejects the shadow rather than resolving it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Surface")
	float StainSourceHeightBias = 0.0f;

	// Faces tilted into the flow catch liquid; faces tilted away shed it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Surface")
	float StainSlopeWeight = 0.0f;

	// How much of the solve comes from the material accumulated below the layer. 1 uses it
	// directly -- roughness for drag and dispersion, roughness against metallic for absorption.
	// 0 makes both neutral.
	//
	// Replaces the separate Roughness and Porosity responses, which read the same channel and so
	// were the same number on any dielectric. Porosity also duplicated Absorption, which already
	// scales how much the material takes up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Surface")
	float StainSurfaceResponse = 1.0f;

	// Kept only so older recipes deserialize without losing fields. The solver resolves a layer
	// mask and shades nothing, so none of the shading controls are read any more.
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Stain resolves a layer mask and shades nothing."))
	FLinearColor StainColor = FLinearColor(0.45f, 0.45f, 0.45f, 1.0f);

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Stain resolves a layer mask and shades nothing."))
	float StainColorAmount = 0.75f;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Stain resolves a layer mask and shades nothing."))
	float StainRoughness = 0.12f;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "The stain solve is full resolution."))
	int32 StainSolveDivisor = 4;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Merged into StainSurfaceResponse; use Absorption to control how much the material takes up."))
	float StainPorosity = 1.0f;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Renamed to StainSurfaceResponse, which now drives absorption as well."))
	float StainRoughnessResponse = 1.0f;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Replaced by stain transport controls."))
	float StainHeightInfluence = 0.5f;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Replaced by stain transport controls."))
	float StainHeightWarp = 0.0f;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Replaced by stain surface weights."))
	float StainHeightBias = -1.0f;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Replaced by stain surface weights."))
	float StainHeightContrast = 1.0f;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Flow is integrated into the stain solve."))
	float StainFlowAmount = 0.0f;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Flow is integrated into the stain solve."))
	int32 StainFlowRadius = 4;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Flow is integrated into the stain solve."))
	int32 StainFlowSmoothing = 3;

	// Erosion. A tileable, stacked directional-stripe filter evaluated in one dispatch.
	// The initial downhill vector is the steepest sampled direction around each texel, which
	// keeps mortar joints, cut stone edges and other hard material features from averaging away.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionAmount = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionStrength = 0.08f;

	// Detail bands. The compositor clamps this to the shader's fixed loop bound.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	int32 ErosionOctaves = 5;

	// Stripe cells across one UV repeat at the coarsest octave. It remains integral so every
	// octave tiles after its frequency doubles.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	int32 ErosionPeriod = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionGain = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionDetail = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionGullyWeight = 0.65f;

	// Partial phase normalization. 0 preserves blended amplitudes; 1 fully normalizes them.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionNormalization = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionRidgeRounding = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionCreaseRounding = 0.0f;

	// Controls how far from the input flats and generated ridge/crease flats new gullies act.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionSlopeOnset = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionFeatureOnset = 1.25f;

	// Magnitude used for internal straight-gully steering. Direction always comes from the
	// measured maximum slope; this only prevents rounded source profiles from weakening it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionAssumedSlope = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionAssumedSlopeAmount = 1.0f;

	// Pixel radius of the sixteen-direction maximum-slope search.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	int32 ErosionSlopeRadius = 2;

	// Optional low-pass for noisy stone. Zero retains sharp brick and masonry boundaries.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionSlopeBlur = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	EMixtormatErosionCurvatureMode ErosionCurvatureMode = EMixtormatErosionCurvatureMode::Valley;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionCavityInfluence = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionCavityOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionCavityRemapMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionCavityRemapMax = 1.0f;

	// The height signal supplies the valley-to-peak fade target. Influence also gates where
	// the final carve is allowed, independently of the layer mask and cavity gate.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionHeightInfluence = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionHeightScale = 1.0f;

	// Optional placement mask owned by Erosion. When unset, the filter keeps using the layer's
	// accumulated authored, generated, and craquelure mask children.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion|Placement")
	TSoftObjectPtr<UMixtormatMask> ErosionMask;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion|Placement")
	TSoftObjectPtr<UTexture2D> ErosionMaskTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion|Placement", meta = (ClampMin = "1"))
	int32 ErosionMaskTiling = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion|Placement")
	bool bErosionInvertMask = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionNormalStrength = 8.0f;

	// Kept only so older recipes deserialize without losing fields. Erosion resolves coverage
	// for height, normal and roughness and no longer authors base colour.
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Erosion no longer authors base colour."))
	FLinearColor ErosionColor = FLinearColor(0.16f, 0.14f, 0.12f, 1.0f);

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Erosion no longer authors base colour."))
	float ErosionColorAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionRoughnessAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionCarveDepth = 0.05f;

	// Grade. Transforms the base colour composited up to this layer, masked by the layer's
	// own mask children, which makes it an adjustment layer rather than a per-texture tweak.
	//
	// Applied in this order, and the order is the point: brightness and contrast are linear
	// operations and belong above the tonemap, gamma is display shaping and belongs below it.
	//
	//     Brightness -> Contrast (about Pivot) -> Tonemap -> Gamma
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade")
	float GradeAmount = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade")
	EMixtormatGradeTonemap GradeTonemap = EMixtormatGradeTonemap::None;

	// Blend between the untonemapped and tonemapped result, so an operator can be dialled in
	// rather than only switched on.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade")
	float GradeTonemapStrength = 1.0f;

	// A gain, not an offset: scaling linear values behaves like exposure and leaves hue
	// alone, where adding a constant washes saturation out of the darks.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade")
	float GradeBrightness = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade")
	float GradeContrast = 1.0f;

	// The value contrast pivots about. 0.18 is linear mid grey and is correct for this data;
	// 0.5 is what display-referred habits reach for, so it is a control rather than a
	// constant.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade")
	float GradeContrastPivot = 0.18f;

	// Applied as pow(c, 1 / Gamma), so above 1 lifts the midtones. That is the convention
	// every grading UI uses and the reciprocal is easy to get backwards.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade")
	float GradeGamma = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade")
	bool bGradeInvertMask = false;

	// Chipping. A smooth height selection mixed with local cavity seeds chips, which grow
	// inward over N iterations and carve the composited height.
	//
	// Raised material comes from thresholding that height at Grout Level, not from a generated
	// lattice, so this works on whatever was actually built -- a tiled brick texture, a plank
	// height map, or craquelure relief.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chipping")
	float ChipAmount = 0.45f;

	// The height that separates raised material from recess. Everything the filter does is a
	// difference of the mask this produces.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chipping")
	float ChipGroutLevel = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chipping")
	float ChipGroutSoftness = 0.08f;

	// Optional seed gates, matching erosion's height and cavity controls. Grout Level still
	// bounds propagation; these decide where inside that material chips are allowed to begin.
	// Defaults preserve the height/cavity mix used before these controls were exposed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chipping|Cavity")
	float ChipCavityInfluence = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chipping|Cavity")
	float ChipCavityOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chipping|Cavity")
	float ChipCavityRemapMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chipping|Cavity")
	float ChipCavityRemapMax = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chipping|Height")
	float ChipHeightInfluence = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chipping|Height")
	float ChipHeightScale = 1.0f;

	// Optional mask owned by Chipping. When unset, the layer's accumulated mask children are used.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chipping|Placement")
	TSoftObjectPtr<UMixtormatMask> ChipMask;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chipping|Placement")
	TSoftObjectPtr<UTexture2D> ChipMaskTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chipping|Placement", meta = (ClampMin = "1"))
	int32 ChipMaskTiling = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chipping|Placement")
	bool bChipInvertMask = false;

	// How slowly a chip loses strength as it grows, and the only thing that attenuates a
	// propagating tip. Spans roughly 7 pixels of reach at 0 to 240 at 1, so Iterations is what
	// caps it in practice at the top of the range.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chipping")
	float ChipSize = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chipping")
	float ChipDepth = 0.035f;

	// Weights a curl noise against the inward direction, so chips wander instead of running
	// straight in from the edge. Also loosens the alignment test that grows them.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chipping")
	float ChipIrregularity = 0.6f;

	// A chip advances one pixel per iteration, so this is the hard bound on how far one can
	// reach. Scaled internally by the render resolution against a 1024 reference, so a preview
	// and an export show the same chip size rather than the same pixel count.
	//
	// Sized so it does not clip Size at its default: Size 0.6 decays to nothing at about 16
	// pixels on its own, and a lower cap here would silently be the thing deciding chip size.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chipping", meta = (ClampMin = "1", ClampMax = "32"))
	int32 ChipIterations = 16;

	// Stands in for the prototype's material-id input: the layer's own mask edge biases where
	// chips start, how long they survive and how deep they cut. Zero by default, so the
	// behaviour is opt-in; inert anyway on a layer whose mask is uniform.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chipping")
	float ChipMaskEdge = 0.0f;

	// Gain on the normal derived from the chip mask. Same meaning and default as the erosion
	// control, because both passes use the same Sobel normalisation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chipping")
	float ChipNormalStrength = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chipping", meta = (ClampMin = "0"))
	int32 ChipSeed = 1;

	// Kept only so older recipes deserialize without losing fields. Chipping resolves coverage
	// for height, normal and roughness and no longer authors base colour.
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Chipping no longer authors base colour."))
	FLinearColor ChipColor = FLinearColor(0.34f, 0.30f, 0.27f, 1.0f);

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Chipping no longer authors base colour."))
	float ChipColorAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chipping")
	float ChipRoughnessAmount = 0.0f;
};

// Two constructions, not two presets for one.
//
// Lattice measures distance to a Voronoi cell wall. Its junctions are three-way at roughly 120
// degrees, because that is what a bisector diagram is, and at Jitter 0 the cells are a regular
// grid -- which is grout: brick, tile, plank.
//
// Propagated grows cracks outward from nuclei and stops them where they meet, which puts the
// junctions at right angles. A crack reaching an older crack stops there, because the older one
// has already released the stress driving it. That T-junction is what a drying film actually
// does and what a bisector diagram cannot produce.
UENUM(BlueprintType)
enum class EMixtormatCraquelureMode : uint8
{
	Lattice UMETA(DisplayName = "Lattice"),
	Propagated UMETA(DisplayName = "Propagated")
};

// Craquelure. A generated crack network used as a mask or carved into height and normals.
//
// Its own node rather than a signal on the generated mask. Everything that node produces is
// derived from the surface beneath it and it early-returns when there is none; craquelure is
// generated rather than derived and means something on the bottom layer, so living there forced
// that early return to be picked apart into a per-signal guard. It is a mask rather than a
// filter so it can drive anything downstream -- chipping placement, a stain, a peel -- rather
// than only cutting the surface itself.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatCraquelure
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure")
	EMixtormatCraquelureMode Mode = EMixtormatCraquelureMode::Propagated;

	// Cells across one UV repeat, and the only scale control there is.
	//
	// Each mode used to have its own: Lattice called it Period and counted Voronoi cells,
	// Propagated called it SeedCells and counted nucleation sites. They answer the same question --
	// how big is a piece of the broken surface -- so they are one number now, read by whichever
	// mode is running. Any integer tiles, because both lattices wrap on it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure", meta = (ClampMin = "1", ClampMax = "128"))
	int32 Scale = 8;

	// 0 puts the cells on a regular lattice and gives grout: brick, tile, plank. 1 gives organic
	// crazing. Also one control from two -- Lattice jittered its cell points and Propagated its
	// nuclei, and hiding the lattice is the same intent either way.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Jitter = 1.0f;

	// In cell units, so it means the same thing at any Scale.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Width = 0.04f;

	// Thins individual cracks, so the network reads as breaks that opened at different times
	// rather than as a uniform lattice. Keyed on the whole crack, not on either side of it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Variation = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure", meta = (ClampMin = "0"))
	int32 Seed = 1;

	// -- Warp ---------------------------------------------------------------------------------
	// Bends the finished network rather than steering how it grows, so it costs a resolve pass and
	// rebuilds nothing. Periodic curl noise: divergence-free, and it wraps on its own period, so
	// the result still tiles.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Warp = 0.0f;

	// Size of the warp's swirls, in repeats across the UV. Its seed comes off Seed, so reseeding
	// the network reseeds the warp with it rather than leaving a second seed to remember.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure", meta = (ClampMin = "1", ClampMax = "32"))
	int32 WarpScale = 4;

	// -- Growth (Propagated mode) ---------------------------------------------------------------
	// Ignored in Lattice mode, which needs none of it: a Voronoi diagram has no growth to steer.

	// How far a crack can reach, in pixels at a 1024 reference, scaled by the render resolution so
	// a preview and an export grow the same network rather than the same pixel count. The dispatch
	// count scales with it, so this is the control that costs.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Growth", meta = (ClampMin = "1", ClampMax = "1024"))
	int32 Iterations = 48;

	// Fraction of cells that get a nucleus at all: how many separate cracks there are, as opposed
	// to how far each one runs.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Growth", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Density = 0.35f;

	// Size of the stress, toughness and flow fields as a multiple of Scale, rather than as a
	// second cell count.
	//
	// It used to be NoiseCells, an absolute number that fought Scale: moving either changed the
	// character, because what decides it is the field's size *relative* to the pieces. Under 1 the
	// fields steer whole regions; over 1 they roughen individual cracks.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Growth", meta = (ClampMin = "0.1", ClampMax = "8.0"))
	float Detail = 0.65f;

	// How far the stress and toughness fields depart from uniform. At 0 the network is steered
	// only by flow and roughness, which reads as combed rather than as fractured.
	//
	// One dial where there were two. Stress driving cracking on and toughness holding it back are
	// two ends of one balance, they read from independent noise either way, and a uniform stress
	// field over a varying toughness one is not a thing anyone reached for.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Growth", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FieldContrast = 0.4f;

	// How strongly those fields win against a tip's own heading. Was StressGain and ToughnessCost:
	// two weights on opposite signs of the same comparison.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Growth", meta = (ClampMin = "0.0", ClampMax = "8.0"))
	float FractureBias = 0.85f;

	// How much a crack is a line rather than a blob. Was Persistence, weighting a tip's preference
	// to carry straight on, and TurnResponse, setting how fast its stored heading caught up -- how
	// hard it resists turning and what happens when it does. They only ever moved together.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Growth", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Straightness = 0.35f;

	// Coherent wander: how much the curl-flow field bends a running crack. At 1 cracks follow the
	// field and come out combed.
	//
	// Deliberately not merged with Roughness. Flow is directional and continuous, Roughness is
	// per-step noise; combed and ragged are different looks and one dial cannot do both.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Growth", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Flow = 0.18f;

	// Incoherent wander: random jitter in the choice of next step. Roughens a crack's edge without
	// giving it anywhere to go.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Growth", meta = (ClampMin = "0.0", ClampMax = "8.0"))
	float Roughness = 0.32f;

	// -- Advanced -------------------------------------------------------------------------------
	// Growth-kernel internals, right for almost everything. Here because between them they decide
	// whether the network terminates like a drying film or keeps running.

	// Score a step has to beat before a tip advances at all.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Advanced", meta = (ClampMin = "0.0", ClampMax = "8.0"))
	float GrowthThreshold = 0.55f;

	// How many existing cracks a tip may touch before it stops. This is what makes the network meet
	// at right angles like a drying film instead of at the 120 degrees a bisector diagram gives,
	// and it is the whole reason the propagated mode exists.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Advanced", meta = (ClampMin = "1", ClampMax = "8"))
	int32 CollisionLimit = 2;

	// -- Relief -----------------------------------------------------------------------------------
	// Two weights on one generated groove: how deep it cuts and how hard the normal follows it.
	// Either at zero switches off that half without touching the other.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Relief", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReliefDepth = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Relief", meta = (ClampMin = "0.0", ClampMax = "32.0"))
	float ReliefNormalStrength = 8.0f;

	// The groove's mouth, and the curve of its wall between a straight V and a rounded U. Not
	// merged: a wide V and a narrow U are both things you would ask for.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Relief", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReliefWidth = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Relief", meta = (ClampMin = "0.05", ClampMax = "8.0"))
	float ReliefProfile = 1.0f;

	// -- Output -----------------------------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Blend")
	EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::Max;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Blend", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Weight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Blend")
	FMixtormatMaskShaping Shaping;
};

// Colour ID mask. Selects the parts of an ID map that carry one of a set of chosen colours.
//
// This is the one placement control that comes from outside the tool. A painted mask, a generated
// one and a curvature mask all describe where something is in the abstract; an ID map describes
// where something is by name, because the person who built the mesh already decided which
// polygons were the handle and which were the panel. Being able to say "the bolts" and have it
// mean the bolts is a different kind of control from anything else in the stack.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatColorIdMask
{
	GENERATED_BODY()

	// The cap on the selection, and the one place it is stated. The shader holds the colours in a
	// fixed constant array and the inspector lays out a fixed set of rows, so both have to agree
	// with this or one of them silently ignores what the other allows.
	static constexpr int32 MaxColors = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID")
	bool bEnabled = true;

	// Import this with sRGB off and no compression. Both settings move the colours the map
	// stores, and a selection is a comparison against a colour the artist chose in a picker: DXT
	// invents intermediate values along every id boundary and shifts flat regions by more than a
	// tight tolerance allows, and an sRGB decode moves every value at once.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID")
	TSoftObjectPtr<UTexture2D> IdTexture;

	// The colours to select, unioned. Several per node because selecting a set is the common
	// case, and one node per colour would be a stack whose blend modes all have to agree just to
	// express an OR.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID")
	TArray<FLinearColor> Colors;

	// How far from a chosen colour still counts, as a distance in RGB. The diagonal of the cube
	// is about 1.73, so this is small by nature: the default admits the compression wobble in a
	// flat region without reaching a neighbouring id.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID", meta = (ClampMin = "0.0", ClampMax = "1.732"))
	float Tolerance = 0.10f;

	// Width of the transition either side of Tolerance. The map is point sampled, so the
	// selection edge is a hard pixel boundary; this is what feathers it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float Softness = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID")
	EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::Replace;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Weight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID")
	bool bInvert = false;

	// Source placement, matching the painted mask. Integer tiling per axis, because a fractional
	// scale lands mid-texel at the UV wrap and seams.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID", meta = (ClampMin = "1"))
	int32 TilingX = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID", meta = (ClampMin = "1"))
	int32 TilingY = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID")
	float UVOffsetX = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID")
	float UVOffsetY = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID")
	bool bFlipU = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID")
	bool bFlipV = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID")
	EMixtormatUVRotation Rotation = EMixtormatUVRotation::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Balance = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float Contrast = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Offset = 0.0f;
};

// Cluster IDs. Segments the surface into regions that follow its own structure and emits an
// integer ID per pixel -- a region label, not coverage.
//
// A Filter rather than a Mask because of what the output *is*. A mask child emits 0..1 coverage
// and composes through a BlendMode and a Weight, and every one of those operations is defined on
// coverage. Ask what Max of two ID maps means, or lerp(idA, idB, 0.5), and the answer is a third
// integer that labels nothing. So there is no BlendMode, no Weight and no shaping chain here:
// they would all be operations the data cannot support. FMixtormatColorIdMask is the other half
// of this -- selecting *from* an ID map is coverage and is correctly a mask.
//
// A Filter rather than an Effect because of where it runs. Effects run after the layer
// composites, which is why craquelure relief had to defer itself. This needs the surface maps as
// input and its output has to exist before the mask chain and the colour stage can read it, so it
// runs ahead of both.
//
// Regions come out of the image: there is no cell size, no cluster count and no compactness,
// because the sizes are whatever the height and roughness say they are. Two scales is a second
// instance at a wider Threshold, not a second algorithm.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatClusterFilter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster IDs")
	bool bEnabled = true;

	// Band width, and so region granularity: the scale control, and the only one there is.
	//
	// Roughness is quantised into bands of this width and two neighbours join only if they land in
	// the same band. Read against a 0..1 signal after the height and roughness have both been
	// renormalised, which is what makes one number mean the same thing on every scan instead of
	// drifting with whatever range that particular texture's height happened to occupy. Scaled by
	// 0.25 inside the kernel, so the dial spans roughly four bands to hundreds.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster IDs", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Threshold = 0.33f;

	// Where the band boundaries fall. Same granularity, different partition -- a reseed that moves
	// every boundary without changing the character, which is the control Threshold cannot be.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster IDs", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Offset = 0.0f;

	// How strongly a step in height blocks a merge that roughness would otherwise allow. The
	// criterion is two-channel: same roughness band *and* |dHeight| * this <= Threshold, so a
	// region has to be uniform in roughness and not step in height. At 0 it is roughness bands
	// alone, which is the right answer on a surface whose height carries no region structure.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster IDs", meta = (ClampMin = "0.0", ClampMax = "8.0"))
	float HeightInfluence = 1.0f;
};

// Per-region HSV variation. Takes the layer's albedo and a cluster filter's ID map, and rewrites
// the colour so every region is a slightly different one.
//
// A Filter for the same reason the cluster is: the output is albedo, not coverage. It has no
// BlendMode and no Weight because there is no mask chain to join -- it replaces what the layer's
// colour is, the way HueShift and Saturation on the layer already do, rather than deciding where
// the layer lands. It is applied at the composite's own albedo sample, immediately before the
// layer's global HueShift/Saturation/Value: per-region jitter first, then the whole-layer grade
// on top, which is the order that reads as "one material, varied" rather than two gradings
// fighting.
//
// Reads the nearest enabled cluster filter above it in the child list. With none, it passes the
// albedo through untouched.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatHsvIdFilter
{
	GENERATED_BODY()

	// Same cap and same reason as FMixtormatColorIdMask::MaxColors: a fixed constant array in the
	// shader and a fixed set of rows in the inspector, both of which have to agree with this.
	static constexpr int32 MaxPaletteColors = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HSV From IDs")
	bool bEnabled = true;

	// The palette regions draw from, sampled as a ramp: entries are evenly spaced stops and a
	// region lands anywhere between two of them.
	//
	// This is the half that matters, and it is what pure HSV jitter cannot do. Jitter can only
	// wander from wherever the texture already sits; a palette lets the variation be *aimed* --
	// brick reds through ochres, or the green-to-grey of weathered copper -- and then jittered
	// around that. Empty means no tint, and the jitter rows below still apply.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HSV From IDs")
	TArray<FLinearColor> Palette;

	// How far a region tints toward the colour it sampled, as a random amount in this range.
	// A range rather than one number so most regions can sit near the texture's own colour with
	// a few pulled much further toward the palette.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HSV From IDs", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RampMixMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HSV From IDs", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RampMixMax = 0.15f;

	// -- Jitter ---------------------------------------------------------------------------------
	// Min/max pairs rather than a single +/- amount, so variation can be *biased*: hue 0 to 0.1
	// shifts only warm. A symmetric amount cannot express that.
	//
	// Hue is added because it is circular; saturation and value are multiplied because they are
	// magnitudes. Keep all of it small -- a couple of percent of hue is already clearly visible on
	// a flat surface, and the target is "same kiln, different firing", not a rainbow.

	// -1..1 maps to +/-180 degrees, the same convention as FMixtormatLayer::HueShift.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HSV From IDs|Jitter", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float HueMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HSV From IDs|Jitter", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float HueMax = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HSV From IDs|Jitter", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float SaturationMin = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HSV From IDs|Jitter", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float SaturationMax = 1.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HSV From IDs|Jitter", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float ValueMin = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HSV From IDs|Jitter", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float ValueMax = 1.1f;

	// Reshuffles which region gets which colour without changing the ranges. Five decorrelated
	// randoms come off one hash of the ID and this seed -- palette position, tint amount, hue,
	// saturation and value -- so moving it moves all five together.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HSV From IDs", meta = (ClampMin = "0"))
	int32 Seed = 1;
};

// Random value per region. Takes a cluster filter's ID map and emits 0..1: every pixel in a
// region gets the same value, different regions get different values.
//
// This one genuinely *is* coverage, which is why it is a mask and not a filter. It blends through
// a BlendMode and a Weight like any other mask child and carries the shared shaping block, and
// every existing consumer of a mask works with it unchanged -- stain only the recessed regions,
// wear only the proud ones, chipping only on the large fragments. Region-aware placement for
// effects that already exist, with no new work on their side.
//
// The precedent is FMixtormatColorIdMask, which is also a mask that consumes an ID map. Selecting
// from an ID map is coverage; producing one is not.
//
// Reads the nearest enabled cluster filter above it in the child list. With none, it contributes
// nothing.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatRandomIdMask
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Random From IDs")
	bool bEnabled = true;

	// The range a region's value is drawn from, before shaping. Min above Max is not an error --
	// it inverts the draw, which is the same picture as bInvert and costs nothing to allow.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Random From IDs", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Random From IDs", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxValue = 1.0f;

	// No zeroinvalid dial, deliberately. The prototype needs one because Houdini hands it an
	// unbound id layer as -1; here the union-find assigns every pixel a root, so there is no
	// pixel outside every region and the control would be a dial that never fires. The shaders
	// still carry the sentinel check, so the day something can produce a region-less pixel --
	// a minimum region size, say -- the handling is already in place.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Random From IDs", meta = (ClampMin = "0"))
	int32 Seed = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Random From IDs")
	EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::Replace;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Random From IDs", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Weight = 1.0f;

	// The shared block, embedded rather than redeclared. Contrast above 1 about the 0.5 midpoint
	// is what the prototype's ramp mode was for: it pulls most regions toward the mean and leaves
	// a few outliers, which is the distribution that reads as subtle instead of as noise.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Random From IDs")
	FMixtormatMaskShaping Shaping;
};

UENUM(BlueprintType)
enum class EMixtormatLayerChildType : uint8
{
	Mask UMETA(DisplayName = "Mask"),
	Effect UMETA(DisplayName = "Effect"),
	Generated UMETA(DisplayName = "Generated Mask"),
	Craquelure UMETA(DisplayName = "Craquelure"),
	ColorId UMETA(DisplayName = "Color ID"),
	// Appended, and they have to stay appended: Type is serialised by value, so inserting
	// anything above this line silently retypes every child in every saved material.
	//
	// Three values rather than one Filter with a sub-kind. Every dispatch in the plugin
	// switches on Type alone -- the badge, the row name, the enable toggle, the remove
	// label, the gather loop -- and a second-level discriminator would grow a nested
	// branch in each of them that the compiler cannot see missing. The Filter *category*
	// lives in the add menu and the FILT badge, which is where it is actually experienced.
	Filter UMETA(DisplayName = "Cluster IDs"),
	HsvFilter UMETA(DisplayName = "HSV From IDs"),
	RandomId UMETA(DisplayName = "Random From IDs")
};

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatLayerChild
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child")
	EMixtormatLayerChildType Type = EMixtormatLayerChildType::Mask;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child", meta = (EditCondition = "Type == EMixtormatLayerChildType::Mask"))
	FMixtormatMaskLayer Mask;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child", meta = (EditCondition = "Type == EMixtormatLayerChildType::Effect"))
	FMixtormatLayerEffect Effect;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child", meta = (EditCondition = "Type == EMixtormatLayerChildType::Generated"))
	FMixtormatGeneratedMask Generated;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child", meta = (EditCondition = "Type == EMixtormatLayerChildType::Craquelure"))
	FMixtormatCraquelure Craquelure;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child", meta = (EditCondition = "Type == EMixtormatLayerChildType::ColorId"))
	FMixtormatColorIdMask ColorId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child", meta = (EditCondition = "Type == EMixtormatLayerChildType::Filter"))
	FMixtormatClusterFilter Filter;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child", meta = (EditCondition = "Type == EMixtormatLayerChildType::HsvFilter"))
	FMixtormatHsvIdFilter HsvFilter;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child", meta = (EditCondition = "Type == EMixtormatLayerChildType::RandomId"))
	FMixtormatRandomIdMask RandomId;
};

namespace MixtormatHue
{
	// Degrees of hue rotation per unit of a normalised -1..1 editor slider.
	//
	// Half the circle, so full deflection either way reaches every hue and the two ends of the
	// slider land on the same colour. This is the number that ties the UI's range to the degrees
	// FMixtormatLayer::HueShift is stored in and the shader's own /360 -- change it and the row
	// stops covering the wheel.
	constexpr double DegreesPerUnit = 180.0;
}

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatLayer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	EMixtormatLayerType Type = EMixtormatLayerType::Material;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	TSoftObjectPtr<UMixtormatSurface> SourceSurface;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	EMixtormatLayerChannelMode ChannelMode = EMixtormatLayerChannelMode::CompleteSurface;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Normal Detail", meta = (EditCondition = "ChannelMode == EMixtormatLayerChannelMode::NormalDetail"))
	EMixtormatNormalSourceType NormalSourceType = EMixtormatNormalSourceType::Surface;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Normal Detail", meta = (EditCondition = "ChannelMode == EMixtormatLayerChannelMode::NormalDetail && NormalSourceType == EMixtormatNormalSourceType::Texture"))
	TSoftObjectPtr<UTexture2D> NormalTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	EMixtormatCompositionMode CompositionMode = EMixtormatCompositionMode::Replace;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Opacity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	bool bOverrideBaseColor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (EditCondition = "bOverrideBaseColor"))
	FLinearColor BaseColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	bool bOverrideRoughness = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (EditCondition = "bOverrideRoughness", ClampMin = "0.0", ClampMax = "1.0"))
	float Roughness = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	bool bOverrideIOR = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (EditCondition = "bOverrideIOR", ClampMin = "1.0", ClampMax = "3.0"))
	float IOR = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	bool bOverrideMetallic = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (EditCondition = "bOverrideMetallic", ClampMin = "0.0", ClampMax = "1.0"))
	float Metallic = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (ClampMin = "1.0", ClampMax = "8.0", Delta = "1.0"))
	float Tiling = 2.0f;

	// UV placement for the layer's source, on top of Tiling.
	//
	// Everything here has to survive the frac() the compositor wraps every source read in, or
	// the surface seams. Scale is per-axis and integer for exactly that reason: a fractional
	// factor lands mid-cell at the wrap. Offset and flip are safe at any value -- translating
	// and mirroring a periodic function leave it periodic.
	//
	// There is deliberately no rotation. It breaks the repeat at every angle that is not a
	// multiple of 90 degrees, and a control that seams across most of its range is worse than
	// no control, so rotation is offered as quarter turns only -- see Rotation below.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (ClampMin = "1", ClampMax = "16"))
	int32 UVScaleX = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (ClampMin = "1", ClampMax = "16"))
	int32 UVScaleY = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float UVOffsetX = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float UVOffsetY = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments")
	bool bFlipU = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments")
	bool bFlipV = false;

	// The piece the layer transform was missing. Excluded originally because arbitrary rotation
	// breaks the frac() wrap, which quarter turns do not -- they are permutations of the unit
	// square. Applied before the tiling, so the surface turns and the lattice replicates the
	// turned result rather than each cell turning in place.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments")
	EMixtormatUVRotation Rotation = EMixtormatUVRotation::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RoughnessBias = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float RoughnessContrast = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (ClampMin = "-0.5", ClampMax = "0.5"))
	float RoughnessOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float NormalIntensity = 1.0f;

	// Degrees. The composite pass divides by 360 and wraps, so the clamp is a half turn either
	// way -- past that a shift is indistinguishable from the shorter rotation the other side.
	// Editor rows are normalised -1..1 and scale by MixtormatHue::DegreesPerUnit to get here.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Adjustments", meta = (ClampMin = "-180.0", ClampMax = "180.0"))
	float HueShift = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Adjustments", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Saturation = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Adjustments", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Value = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments")
	EMixtormatNormalBlendMode NormalBlendMode = EMixtormatNormalBlendMode::Combine;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending")
	bool bHeightBlendEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled"))
	EMixtormatHeightSource HeightSource = EMixtormatHeightSource::LayerHeight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (DisplayName = "Mask Strength", EditCondition = "bHeightBlendEnabled", ClampMin = "0.0", ClampMax = "4.0"))
	float HeightBlendAmount = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled", ClampMin = "0.0", ClampMax = "1.0"))
	float HeightThreshold = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (DisplayName = "Softness", EditCondition = "bHeightBlendEnabled", ClampMin = "0.0"))
	float HeightRange = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled", ClampMin = "0.01"))
	float HeightContrast = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (DisplayName = "Blend Height Bias", EditCondition = "bHeightBlendEnabled", ClampMin = "-1.0", ClampMax = "1.0"))
	float HeightOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (DisplayName = "Base Height Bias", EditCondition = "bHeightBlendEnabled", ClampMin = "-1.0", ClampMax = "1.0"))
	float HeightBias = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled"))
	bool bInvertHeight = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled", ClampMin = "0.0", ClampMax = "1.0"))
	float ConstantHeight = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled", ClampMin = "0.0", ClampMax = "1.0"))
	float MaskHeightInfluence = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled", ClampMin = "0.0", ClampMax = "1.0"))
	float HeightContactAOAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled", ClampMin = "0.0001", ClampMax = "1.0"))
	float HeightContactAOWidth = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled", ClampMin = "-1.0", ClampMax = "1.0"))
	float HeightBorderLift = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled", ClampMin = "0.0001", ClampMax = "1.0"))
	float HeightBorderWidth = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled", ClampMin = "0.0", ClampMax = "8.0"))
	float HeightBorderNormalStrength = 1.0f;

	// Gaussian radius, in texels, applied to the accumulated height that Contact AO and Border
	// Normal are built from. 1 skips the two blur passes entirely.
	//
	// Width is a softness in the height domain: it widens the band without changing how the field
	// is sampled, so raising it gave a wider band that was just as noisy. This smooths the height
	// before the field is derived from it, and only those two effects read the smoothed copy --
	// coverage, layer height and blend weight all keep the sharp one.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled", ClampMin = "1.0", ClampMax = "32.0"))
	float HeightBorderSmoothing = 1.0f;

	// Rounds the height the placement mask produces, in texels. 0 skips the two blur passes
	// entirely. Wide enough and the interior of a shape domes rather than only its rim softening,
	// which is the difference between an anti-aliased edge and a filleted one.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled", ClampMin = "0.0", ClampMax = "32.0"))
	float HeightSmoothRadius = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled", ClampMin = "0.0", ClampMax = "1.0"))
	float HeightSmoothAmount = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled"))
	int32 HeightReferenceLayerIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Children", meta = (TitleProperty = "Type"))
	TArray<FMixtormatLayerChild> Children;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FeatureInfluence = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FeatureBias = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HeightFeatureInfluence = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features")
	bool bInvertHeightFeature = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AOFeatureInfluence = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features")
	bool bInvertAOFeature = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features", meta = (ClampMin = "1", ClampMax = "32"))
	int32 CurvatureRadius = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features", meta = (ClampMin = "0.0"))
	float CurvatureStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features", meta = (ClampMin = "0.001"))
	float CurvaturePower = 1.0f;

	// Averages curvature over this many widening rings. 1 is a single kernel.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features", meta = (ClampMin = "1", ClampMax = "4"))
	int32 CurvatureSmoothing = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features")
	bool bFlipNormalY = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel Influence", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BaseColorInfluence = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel Influence", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RoughnessInfluence = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel Influence", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AOInfluence = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel Influence", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MetallicInfluence = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel Influence", meta = (DisplayName = "IOR / F0 Influence", ClampMin = "0.0", ClampMax = "1.0"))
	float F0Influence = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel Influence", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NormalInfluence = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel Influence", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HeightInfluence = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features")
	bool bInvertFeature = false;
};

UCLASS(BlueprintType)
class MIXTORMATRUNTIME_API UMixtormatMaterial final : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual void PostLoad() override;
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	UFUNCTION(BlueprintPure, Category = "Mixtormat")
	bool CanAddLayer() const;

	UFUNCTION(BlueprintCallable, Category = "Mixtormat")
	bool AddLayer(EMixtormatLayerType Type);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layers", meta = (TitleProperty = "DisplayName"))
	TArray<FMixtormatLayer> Layers;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bake")
	TSoftObjectPtr<UTexture2D> BakedBaseColor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bake")
	TSoftObjectPtr<UTexture2D> BakedNormal;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bake")
	TSoftObjectPtr<UTexture2D> BakedRAM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bake")
	TSoftObjectPtr<UTexture2D> BakedHeight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bake")
	TSoftObjectPtr<UMaterialInterface> BakedMaterial;
};
