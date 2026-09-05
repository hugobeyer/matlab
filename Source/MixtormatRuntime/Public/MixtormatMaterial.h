#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UObject/SoftObjectPtr.h"
#include "MixtormatEffect.h"
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
	bool bInvert = false;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float Balance = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float Contrast = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Offset = 0.0f;
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain")
	FLinearColor StainColor = FLinearColor(0.22f, 0.09f, 0.035f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain")
	float StainRoughness = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain")
	float StainHeightInfluence = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain")
	float StainHeightWarp = 0.0f;

	// Runs the stain along the V axis the way liquid runs down a wall, over a distance Warp
	// sets. Signed, because whether +V is down depends on how the mesh was unwrapped and not
	// on anything the material can know -- negate it if the drips run the wrong way.
	//
	// Not derived from FlipNormalY: that flag is about normal-map green-channel encoding, and
	// coupling the two would flip every drip on a normal map re-import.
	//
	// Unlike the height warp this is not gated by slope, so it still runs on a flat wall --
	// which is the surface it is named for. Inert while Warp is 0, which is its default, so a
	// stain only gravitates once there is a distance for it to travel.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain")
	float StainGravity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain")
	float StainHeightBias = -1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain", meta = (ClampMin = "0.01"))
	float StainHeightContrast = 1.0f;

	// How much the run follows the surface's coherent flow rather than the local uphill
	// gradient. 0 is the original two-texel gradient trace and skips the field entirely, so
	// a stain that does not ask for flow costs nothing extra.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain")
	float StainFlowAmount = 0.0f;

	// Pixel radius of the gradient the orientation field is built from, and how many times
	// it is smoothed. Same meaning as the erosion pair; separate fields because a stain runs
	// over a different height, at a different resolution, in a different place in the graph.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain")
	int32 StainFlowRadius = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain")
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	FLinearColor ErosionColor = FLinearColor(0.16f, 0.14f, 0.12f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
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

	// What the chip exposes. Same shape as the erosion shade controls and the same shader,
	// but the coverage is read from the chip mask rather than reconstructed from a height
	// difference -- so colour still appears at Depth 0, where a difference would be nothing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chipping")
	FLinearColor ChipColor = FLinearColor(0.34f, 0.30f, 0.27f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chipping")
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

	// Cells across one UV repeat. Any integer tiles, because the lattice wraps on it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure", meta = (ClampMin = "1"))
	int32 Period = 16;

	// 0 puts the cells on a regular lattice and gives grout: brick, tile, plank. 1 gives
	// organic crazing. One control spans tile seams to cracked paint.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure")
	float Jitter = 1.0f;

	// In cell units, so it means the same thing at any Period.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure")
	float Width = 0.04f;

	// Thins individual cracks, so the network reads as breaks that opened at different
	// times rather than as a uniform lattice. Keyed on the wall between two cells rather
	// than on either cell, so a crack varies as one thing instead of splitting down its
	// centre.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure")
	float Variation = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure", meta = (ClampMin = "0"))
	int32 Seed = 1;

	// Displaces the lattice so cracks wander instead of following a visibly regular network.
	// Driven by periodic gradient noise, not by a second cellular field. A cellular
	// displacement jumps wherever the nearest feature point changes, which tears the crack
	// network along every Voronoi boundary; gradient noise is continuous, and this one wraps
	// on its own period, so the result still tiles.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure")
	float Warp = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure", meta = (ClampMin = "1"))
	int32 WarpPeriod = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure", meta = (ClampMin = "0"))
	int32 WarpSeed = 7;

	// -- Propagated mode ---------------------------------------------------------------------
	// Ignored in Lattice mode. Period and Jitter are the reverse: Lattice only.

	// How far a crack can reach, in pixels at a 1024 reference. Scaled by the render resolution
	// so a preview and an export grow the same network rather than the same pixel count; the
	// dispatch count scales with it, so this is the control that costs.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Propagated", meta = (ClampMin = "1", ClampMax = "1024"))
	int32 Iterations = 48;

	// Nuclei are placed one per cell of this lattice, jittered inside it and admitted by
	// chance. Fewer, longer cracks come from a coarse lattice; crazing from a fine one.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Propagated", meta = (ClampMin = "1", ClampMax = "128"))
	int32 SeedCells = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Propagated", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SeedChance = 0.35f;

	// 0 puts every nucleus at its cell centre, which the grown network still shows. 1 hides the
	// lattice entirely.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Propagated", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SeedJitter = 0.85f;

	// Scale of the stress, toughness and flow fields. Below the seed lattice it steers whole
	// regions; above it, it roughens individual cracks.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Propagated", meta = (ClampMin = "1"))
	int32 NoiseCells = 5;

	// Stress drives cracking on, toughness holds it back, and the variations are how much each
	// field departs from uniform. With both at zero the network is steered only by flow and
	// irregularity, which reads as combed rather than as fractured.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Propagated", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StressVariation = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Propagated", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ToughnessVariation = 0.45f;

	// How strongly a tip keeps its heading. This is what makes a crack a line rather than a
	// blob: drop it and the front spreads in every direction at once.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Propagated", meta = (ClampMin = "0.0", ClampMax = "1024.0"))
	float Persistence = 1.65f;

	// How much the flow field bends a running crack. At 1 cracks follow the field and come out
	// combed; the default lets the field suggest a direction without dictating it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Propagated", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FlowStrength = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Propagated", meta = (ClampMin = "0.0", ClampMax = "32.0"))
	float StressGain = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Propagated", meta = (ClampMin = "0.0"))
	float ToughnessCost = 0.95f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Propagated", meta = (ClampMin = "0.0", ClampMax = "32.0"))
	float Irregularity = 0.32f;

	// The score a step has to beat to happen at all. Raising it starves growth, which is the
	// control that decides how much of the surface ends up cracked.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Propagated")
	float GrowthThreshold = 0.55f;

	// How fast a tip turns toward the step it just took. Low values curve, high values snap to
	// the eight-way lattice and show the staircase.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Propagated", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TurnResponse = 0.72f;

	// Cracked neighbours a pixel may already have and still be grown into. This is the whole
	// T-junction behaviour: at 2 a crack reaching an older one stops, and raising it lets them
	// cross, which reads as scratches rather than as fracture.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Propagated", meta = (ClampMin = "1", ClampMax = "8"))
	int32 CollisionLimit = 2;

	// Relief. Three independent weights on one generated network, rather than a mode that picks
	// between them: a crack usually wants to mask, carve and catch light at once, and an
	// either/or made that impossible to author. Weight below is the mask weight; these two are
	// the height and normal weights, and each is its own off switch at zero.
	//
	// Carved after the layer composites, from the distance field rather than from the shaped
	// mask. Both modes produce that field -- the lattice knows its cell-boundary distance
	// analytically, the propagated mode floods one out of its grown skeleton.
	// Both default on. Every craquelure node already saved carries these values, because the
	// output mode that used to gate them never dispatched anything -- so defaulting them to zero
	// would have made a new node behave differently from an old one for no reason a user could
	// see. A crack that does not cut is also the wrong default for the thing it is named after.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Relief", meta = (ClampMin = "0.0"))
	float ReliefDepth = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Relief", meta = (ClampMin = "0.0"))
	float ReliefNormalStrength = 8.0f;

	// Half-width of the groove, in cell units like Width. Independent of Width because the two
	// describe different things: Width is the hairline the mask draws, this is the mouth of the
	// depression around it, and a crack that reads as a fine dark line usually sits in a much
	// wider dish.
	//
	// Defaulted at twice Width rather than higher. Past roughly half a cell the grooves of
	// neighbouring cracks overlap everywhere and the surface sinks by a constant instead of
	// showing cracks -- the profile is still correct, there is simply nothing left above it to
	// read against.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Relief", meta = (ClampMin = "0.002"))
	float ReliefWidth = 0.08f;

	// Shape of the groove wall. 1 is the straight cone the distance field gives directly, which
	// is the constant-slope fracture case; below 1 it flares to a dish, above it draws in to a
	// narrow V with a broad flat shoulder.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Relief", meta = (ClampMin = "0.05", ClampMax = "8.0"))
	float ReliefProfile = 1.0f;

	// The same shaping tail every mask-producing node has. Weight is the mask weight only; the
	// height and normal weights live in Relief above.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Blend")
	EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::Max;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Blend")
	bool bInvert = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Blend")
	float Weight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Blend")
	float Balance = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Blend")
	float Contrast = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Blend")
	float Offset = 0.0f;
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

UENUM(BlueprintType)
enum class EMixtormatLayerChildType : uint8
{
	Mask UMETA(DisplayName = "Mask"),
	Effect UMETA(DisplayName = "Effect"),
	Generated UMETA(DisplayName = "Generated Mask"),
	Craquelure UMETA(DisplayName = "Craquelure"),
	ColorId UMETA(DisplayName = "Color ID")
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
};

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (EditCondition = "bHeightBlendEnabled"))
	int32 HeightReferenceLayerIndex = INDEX_NONE;

	// Serialized only to migrate the original single-mask recipe format.
	UPROPERTY()
	TSoftObjectPtr<UMixtormatMask> Mask;

	UPROPERTY()
	TSoftObjectPtr<UTexture2D> MaskTexture;

	UPROPERTY()
	float MaskTiling = 1.0f;

	UPROPERTY()
	float MaskBalance = 0.5f;

	UPROPERTY()
	float MaskContrast = 1.0f;

	UPROPERTY()
	bool bInvertMask = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Children", meta = (TitleProperty = "Type"))
	TArray<FMixtormatLayerChild> Children;

	// Serialized only to migrate recipes created before ordered children.
	UPROPERTY()
	TArray<FMixtormatMaskLayer> Masks;

	// Serialized only to migrate recipes created before ordered children.
	UPROPERTY()
	TArray<FMixtormatLayerEffect> Effects;

	void GetEffectiveMasks(TArray<FMixtormatMaskLayer>& OutMasks) const;
	void MigrateLegacyChildren();

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
