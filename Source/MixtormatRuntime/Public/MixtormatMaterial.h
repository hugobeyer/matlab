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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask", meta = (ClampMin = "1", ClampMax = "16"))
	int32 Tiling = 1;

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
			|| !FMath::IsNearlyZero(HeightWeight);
	}
};

UENUM(BlueprintType)
enum class EMixtormatErosionDirectionMode : uint8
{
	Weight = 0 UMETA(DisplayName = "Weight"),
	Lerp = 1 UMETA(DisplayName = "Lerp")
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling", meta = (ClampMin = "0.0", ClampMax = "1.0"))
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float StainRoughness = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StainHeightInfluence = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StainHeightWarp = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float StainHeightBias = -1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain", meta = (ClampMin = "0.01"))
	float StainHeightContrast = 1.0f;

	// Erosion. Carves the height accumulated below the owning layer.
	//
	// None of these carry ClampMin/ClampMax. The inspector constrains each scrub range
	// visually, but a typed value outside that range reaches the shader intact; the shader
	// keeps epsilon guards at its own division sites. Widening a range is therefore a UI
	// edit, not a schema change.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionAmount = 1.0f;

	// Compatibility only. The pass count is fixed at 8 and the period at 32 cells in the
	// compositor; these remain serialized so recipes written before that still load.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	int32 ErosionIterations = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	int32 ErosionPeriod = 8;

	// Critical slope. Nothing carves below it. Units are height per UV, so the useful
	// value depends entirely on the composited height range.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionRepose = 0.30f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionReposeSoftness = 0.25f;

	// Radius in pixels of the local slope measurement. Small values keep the carve
	// following real surface detail rather than a block-quantised average.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	int32 ErosionSlopeRadius = 2;

	// Low-passes a guidance copy of the height before measuring slope, so surface grain
	// does not steer the flow. 0 measures slope on the raw height.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionSlopeBlur = 2.0f;

	// Signed. Positive concentrates carving in concave regions, negative inverts it onto
	// convex ridges. Past 1 it acts as contrast expansion rather than a blend.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionCavityBias = 0.0f;

	// Smoothstep contrast on the concavity signal. Drives how sharply concave separates
	// from convex before the gate; high values approach a hard edge.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionCavityScale = 1.0f;

	// Signed. Positive erodes raised ground first, negative erodes low ground.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionHeightInfluence = 0.0f;

	// Smoothstep contrast on the height signal, centred on mid height.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionHeightScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionGullyWeight = 2.0f;

	// Authored flow direction blended against downhill. Weight adds it to the slope vector
	// so it competes with steepness; Lerp blends the directions evenly. 0 amount is pure
	// slope flow under either mode.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	EMixtormatErosionDirectionMode ErosionDirectionMode = EMixtormatErosionDirectionMode::Weight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionDirectionAngle = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionDirectionAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionBlendSoftness = 0.0f;

	// How strongly the carve perturbs the layer normal. 0 changes height only.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	float ErosionNormalStrength = 8.0f;
};

UENUM(BlueprintType)
enum class EMixtormatLayerChildType : uint8
{
	Mask UMETA(DisplayName = "Mask"),
	Effect UMETA(DisplayName = "Effect"),
	Generated UMETA(DisplayName = "Generated Mask")
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
