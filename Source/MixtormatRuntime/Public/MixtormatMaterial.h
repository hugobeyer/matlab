// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UObject/SoftObjectPtr.h"
#include "MixtormatEffect.h"
#include "MixtormatMaskBlur.h"
#include "MixtormatMaskCurvature.h"
#include "MixtormatMaskShaping.h"
#include "MixtormatMaterial.generated.h"

class UMaterialInterface;
class UMixtormatEffect;
class UMixtormatMask;
class UMixtormatMaterial;
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
	Overlay UMETA(DisplayName = "Overlay"),
	// Appended, never reordered: these are serialised by value, so inserting above would
	// silently re-map every mask layer already authored against the old ordering.
	Difference UMETA(DisplayName = "Difference"),
	Exclusion UMETA(DisplayName = "Exclusion")
};

// Where a Mask child reads its scalar from.
//
// Texture is the authored map or the published output a mask has always read. Layer Values reads
// the layer the mask is on instead: its own resolved albedo and roughness, so a mask can be the
// shape of the material rather than a painted map. Everything downstream is unchanged -- the
// placement, the shaping, the blur and curvature children, the blend into the chain -- because
// this names the source and nothing else.
UENUM(BlueprintType)
enum class EMixtormatMaskSource : uint8
{
	Texture = 0 UMETA(DisplayName = "Texture"),
	LayerValues = 1 UMETA(DisplayName = "Layer Values")
};

// Which scalar comes out of the layer's resolved values.
//
// The numbering is the shader's: MixtormatMask.usf switches on this directly, so these are not
// free to reorder. Luminance is Rec. 709 over linear RGB, which is what the compositor holds.
UENUM(BlueprintType)
enum class EMixtormatLayerValueChannel : uint8
{
	Luminance = 0 UMETA(DisplayName = "Luminance"),
	Red = 1 UMETA(DisplayName = "Red"),
	Green = 2 UMETA(DisplayName = "Green"),
	Blue = 3 UMETA(DisplayName = "Blue"),
	// No Alpha: the compositor's albedo carries none -- every layer writes 1 there -- so the
	// fourth channel of the resolved values is the layer's roughness instead, which is a signal
	// an artist actually has.
	Roughness = 4 UMETA(DisplayName = "Roughness")
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


UENUM(BlueprintType)
enum class EMixtormatParameterOwnerType : uint8
{
	Layer UMETA(DisplayName = "Layer"),
	Mask UMETA(DisplayName = "Mask"),
	Effect UMETA(DisplayName = "Effect"),
	Generated UMETA(DisplayName = "Generated Mask"),
	Craquelure UMETA(DisplayName = "Craquelure"),
	ColorId UMETA(DisplayName = "Color ID"),
	ClusterId UMETA(DisplayName = "Cluster IDs"),
	HsvId UMETA(DisplayName = "HSV From IDs"),
	RandomId UMETA(DisplayName = "Random From IDs"),
	PatternId UMETA(DisplayName = "Pattern IDs"),
	RampId UMETA(DisplayName = "Ramp From IDs"),
	MaskShaping UMETA(DisplayName = "Mask Shaping"),
	Blur UMETA(DisplayName = "Blur"),
	Curvature UMETA(DisplayName = "Curvature"),
	// Appended, like everything above it. The owner names the *category*, not the generator
	// kind: the parameter view resolves to whichever payload FMixtormatGenerator::Type selects,
	// so a second generator gets its parameters bound without a second owner value.
	Generator UMETA(DisplayName = "Generator")
};

UENUM(BlueprintType)
enum class EMixtormatParameterValueType : uint8
{
	Float UMETA(DisplayName = "Float"),
	Int UMETA(DisplayName = "Integer"),
	Bool UMETA(DisplayName = "Boolean"),
	Enum UMETA(DisplayName = "Enum")
};

UENUM(BlueprintType)
enum class EMixtormatDriverCombineMode : uint8
{
	Replace UMETA(DisplayName = "Replace"),
	Multiply UMETA(DisplayName = "Multiply"),
	Add UMETA(DisplayName = "Add"),
	Subtract UMETA(DisplayName = "Subtract"),
	Min UMETA(DisplayName = "Min"),
	Max UMETA(DisplayName = "Max"),
	Lerp UMETA(DisplayName = "Lerp")
};

UENUM(BlueprintType)
enum class EMixtormatDriverSourceKind : uint8
{
	None UMETA(DisplayName = "None"),
	CombinedMask UMETA(DisplayName = "Layer Mask"),
	ChildMask UMETA(DisplayName = "Child Mask"),
	RegionIds UMETA(DisplayName = "Region IDs")
};

UENUM(BlueprintType)
enum class EMixtormatIdDriverMapping : uint8
{
	RandomPerId UMETA(DisplayName = "Random Per ID"),
	SpecificId UMETA(DisplayName = "Specific ID"),
	IdRange UMETA(DisplayName = "ID Range")
};

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatParameterAddress
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid LayerId;

	UPROPERTY()
	FGuid ChildId;

	UPROPERTY()
	EMixtormatParameterOwnerType Owner = EMixtormatParameterOwnerType::Layer;

	UPROPERTY()
	FName Parameter;

	UPROPERTY()
	EMixtormatParameterValueType ValueType = EMixtormatParameterValueType::Float;

	// Used for enum compatibility. Empty for scalar/bool types.
	UPROPERTY()
	FName TypeName;

	bool IsValid() const
	{
		return LayerId.IsValid() && !Parameter.IsNone();
	}
};

// What a reference does when the destination is edited.
//
// Follow is one-way: the destination shows the source and an edit to the destination is an edit
// to the destination, which is what breaks the reference. Link makes the pair one value with two
// places to reach it -- an edit at either end lands on the authoritative source, so neither side
// is the copy. References only; a GPU Driver modulates a value it does not own and has nothing to
// write back to.
UENUM(BlueprintType)
enum class EMixtormatReferenceMode : uint8
{
	Follow UMETA(DisplayName = "Follow Source"),
	Link   UMETA(DisplayName = "Link Values")
};

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatParameterReference
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reference")
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reference")
	FMixtormatParameterAddress Source;

	// Follow by default, so everything already serialized keeps the one-way behaviour it was
	// authored with.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reference")
	EMixtormatReferenceMode Mode = EMixtormatReferenceMode::Follow;
};

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatParameterDriver
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	FGuid SourceLayerId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	FGuid SourceChildId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	EMixtormatDriverSourceKind SourceKind = EMixtormatDriverSourceKind::None;

	// Optional published output name. This keeps the serialized model extensible without making
	// every future mask/effect output another enum value.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	FName SourceOutput;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	EMixtormatDriverCombineMode Combine = EMixtormatDriverCombineMode::Multiply;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	float Amount = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	float InputMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	float InputMax = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	float OutputMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	float OutputMax = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	bool bInvert = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	EMixtormatIdDriverMapping IdMapping = EMixtormatIdDriverMapping::RandomPerId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	int32 Seed = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	int32 SpecificId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	int32 IdRangeMin = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	int32 IdRangeMax = 255;

	// The range one region's random draw lands in, before the Driver chain's own remap. Separate
	// from OutputMin/OutputMax on purpose: this shapes the signal a region *produces*, the remap
	// shapes what the chain does with any signal, and collapsing them would make the Output rows
	// mean two different things depending on the source.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	float IdRandomMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Driver")
	float IdRandomMax = 1.0f;
};

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatParameterBinding
{
	GENERATED_BODY()

	UPROPERTY()
	FName DestinationParameter;

	UPROPERTY()
	EMixtormatParameterOwnerType DestinationOwner = EMixtormatParameterOwnerType::Layer;

	UPROPERTY()
	EMixtormatParameterValueType ValueType = EMixtormatParameterValueType::Float;

	UPROPERTY()
	FName TypeName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	FMixtormatParameterReference Reference;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	FMixtormatParameterDriver Driver;
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

	// Optional live scalar output published by another child. This is intentionally separate
	// from FMixtormatLayerChild::SourceLayerId/SourceChildId: those make the whole child an
	// instance and require source/destination types to match. A published mask remains an
	// ordinary Mask child, so its Blend Mode, Weight, Shaping and UV controls stay local.
	UPROPERTY()
	FGuid PublishedSourceLayerId;

	UPROPERTY()
	FGuid PublishedSourceChildId;

	UPROPERTY()
	FName PublishedSourceOutput;

	bool HasPublishedSource() const
	{
		return PublishedSourceLayerId.IsValid()
			&& PublishedSourceChildId.IsValid()
			&& !PublishedSourceOutput.IsNone();
	}

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

	// Appended, and defaulted to the behaviour every saved mask already has: Texture reads the
	// authored map exactly as before, so a material written before Layer Values existed loads
	// unchanged and needs no upgrade path.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask")
	EMixtormatMaskSource Source = EMixtormatMaskSource::Texture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mask", meta = (EditCondition = "Source == EMixtormatMaskSource::LayerValues"))
	EMixtormatLayerValueChannel LayerValueChannel = EMixtormatLayerValueChannel::Luminance;

	bool UsesLayerValues() const
	{
		// A published source wins: it is an explicit wiring to another child's output, and the
		// mask has no business second-guessing it.
		return Source == EMixtormatMaskSource::LayerValues && !HasPublishedSource();
	}
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Mask|Blend", meta = (ClampMin = "0.0", UIMax = "4.0"))
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

	// How far the sheet rises where it is still attached beside a tear.
	//
	// Retuned from 0.04 together with the curl length it is spread over. The old pair described a
	// rise of 0.04 across roughly 430 texels, a slope of 0.0001, which is geometrically flat --
	// it only ever read because the peel's normal pass exaggerated its gradient about 256x. With
	// that pass on the shared height->normal convention the lift has to be real relief, so it is
	// now a few millimetres over a few centimetres of sheet rather than a hair over a fifth of
	// the tile.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling", meta = (ClampMin = "0.0"))
	float Lift = 0.2f;

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
	// random value, so some sit almost flat and others stand well clear. Centred on 1, so raising
	// it redistributes lift between flakes without changing the average.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural")
	float PeelLiftVariation = 0.6f;

	// Extra lift on corners and tongues of the remaining sheet, from the convexity of the peel
	// front. 0 lifts every point the same for its distance from the tear, which is what the peel
	// did before this existed and which reads as a uniform rolled hem; 1 doubles the lift where
	// the sheet is held on fewest sides. Paper fails at its corners, so this is most of what
	// separates a peel that looks torn from one that looks hemmed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PeelCornerLift = 0.6f;

	// Size of the window the corner detector looks through, as a multiple of the curl length.
	//
	// A wider window responds to broader features and spreads the boost further back from the
	// tip, so raising this lifts bigger pieces of sheet rather than only their sharpest points.
	// Narrow it to pick out fine serrations along a tear.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural", meta = (DisplayName = "Corner Radius", ClampMin = "0.05", ClampMax = "4.0"))
	float PeelCornerRadius = 1.0f;

	// How strongly the peel starts at the boundaries of the ID map above it -- a cluster filter,
	// a pattern, random IDs, colour IDs, whichever ran before this effect in the layer's chain.
	//
	// A weight, not a gate, which is what separates it from Worn Edges. There the ID boundary is
	// the subject and the wear lives on it; here it biases the damage field, so a low value makes
	// the peel merely prefer seams and a high one effectively confines it to them. Wallpaper lets
	// go at its seams and tiling lets go at its joints, so this is usually closer to the truth
	// than a hand-painted mask tracing a pattern the layer already knows about.
	//
	// 0 does not read the ID map at all, which is the default: an existing peel is unchanged
	// until the control is deliberately raised.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural", meta = (DisplayName = "ID Influence", ClampMin = "0.0", ClampMax = "1.0"))
	float PeelIDInfluence = 0.0f;

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
	float StainGravity = -1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Simulation")
	float StainSurfaceFollow = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Simulation")
	float StainSpread = 0.05f;

	// Controls how much liquid remains and pools locally while flow transports the rest.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Simulation")
	float StainAccumulation = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Simulation")
	float StainAbsorption = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Simulation")
	float StainDrying = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Simulation")
	float StainDirtAmount = 0.35f;

	// Existing surface analysis participates directly in source placement. Positive values add
	// liquid/dirt at concave or convex detail; zero leaves placement entirely mask-driven.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Surface")
	float StainConcavityWeight = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Surface")
	float StainConvexityWeight = 0.45f;

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
	float StainSlopeWeight = 0.25f;

	// How much of the solve comes from the material accumulated below the layer. 1 uses it
	// directly -- roughness for drag and dispersion, roughness against metallic for absorption.
	// 0 makes both neutral.
	//
	// Replaces the separate Roughness and Porosity responses, which read the same channel and so
	// were the same number on any dielectric. Porosity also duplicated Absorption, which already
	// scales how much the material takes up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Surface")
	float StainSurfaceResponse = 1.0f;

	// Runoff: the cheap procedural streak.
	//
	// Ten controls, and everything else derived from them. The rule the whole effect is built
	// around is that an artist tuning a dirty run is choosing how far it goes, how soft it is and
	// how broken up it looks -- not a strata count, a lip width or an octave count. Those follow
	// from the four that matter, and are documented where they are derived rather than exposed
	// here as another dozen sliders to get wrong.
	//
	// What it actually does: cavities and downhill ledges in the accumulated height say where
	// runoff starts, the incoming mask says how much starts there, a small-feature fractal noise
	// breaks it up, and the result is smeared along one axis with a directional Gaussian. Several
	// strata of that smear, each a slightly different length and each warped only along gravity,
	// stack into something that reads as overlapping deposits rather than one blurred streak.

	// Which way runoff runs, in degrees. -90 is straight down the texture, which is what gravity
	// means on a wall. Runoff is strictly one-directional: it never spreads sideways off its own
	// axis the way a transport solve does, which is exactly what makes it a smear and not a solve.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runoff", meta = (ClampMin = "-180.0", ClampMax = "180.0"))
	float RunoffGravityAngle = -90.0f;

	// How far runoff reaches from its source, in texels at 1K. Read as a fraction of the longer
	// side rather than as literal texels, so the same number is the same run at 1K, 2K and 4K --
	// a streak that shortened when the composition grew would make the control meaningless.
	//
	// This is the reach of the strongest source. A weaker mask value reaches proportionally less,
	// which is what makes the incoming mask a length control and not only an opacity.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runoff", meta = (ClampMin = "8.0", ClampMax = "512.0"))
	float RunoffStreakRadius = 320.0f;

	// The Gaussian's sigma as a fraction of reach. Low values keep a run tight and defined and
	// stop it abruptly; high values let it fade out over most of its length. It also widens the
	// terminal lip, because a soft run deposits over a longer stretch than a sharp one does.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runoff", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float RunoffStreakSoftness = 0.46f;

	// How much the height underneath decides where runoff starts. At 1 only cavities and the
	// upper edges of ledges source it, which is where dirt actually collects. At 0 the height is
	// ignored and the incoming mask alone is the source, which is how to streak from a painted
	// mark rather than from the surface's own shape.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runoff", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RunoffSurfaceInfluence = 0.95f;

	// How strongly the stacked layers read as separate deposits. Not a count: it widens their
	// spacing, spreads their lengths and flattens their opacity falloff all at once, so 0 is a
	// single coherent run and 1 is a visibly layered, uneven buildup. The count itself follows
	// from Streak Radius -- a short run has no room to show five strata.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runoff", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RunoffStrataAmount = 0.75f;

	// Feature size of the internal fractal noise, as cells across the texture. It does double
	// duty deliberately: the same field breaks up the source before the smear and warps each
	// stratum's length, so one control changes the grain of the whole effect rather than needing
	// a separate noise scale nobody would match to it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runoff", meta = (ClampMin = "1.0", UIMax = "32.0"))
	float RunoffWarpScale = 18.0f;

	// How far that noise pushes each stratum's endpoint along gravity. Only along gravity -- a
	// sideways push would turn a run into a smudge. Above 1 the strata pull apart far enough to
	// read as independent runs from the same source.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runoff", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float RunoffWarpAmount = 1.5f;

	// The narrow deposit left where a run stops, the way a drying streak leaves a tidemark. 0
	// ends every run on a clean fade; 1 puts a defined crust at the end of each stratum.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runoff", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RunoffLipStrength = 0.55f;

	// Overall weight of the resolved runoff in the layer's mask chain. 0 is the identity.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runoff", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RunoffStrength = 0.25f;

	// Every random choice the effect makes -- the noise field, the per-stratum decorrelation --
	// comes off this. Same seed, same runoff, at any resolution.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runoff", meta = (ClampMin = "0", ClampMax = "9999"))
	int32 RunoffSeed = 1;

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
	float ErosionStrength = 0.5f;

	// Detail bands. The compositor clamps this to the shader's fixed loop bound.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	int32 ErosionOctaves = 2;

	// Stripe cells across one UV repeat at the coarsest octave. It remains integral so every
	// octave tiles after its frequency doubles.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion")
	int32 ErosionPeriod = 4;

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

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Erosion normals derive from the final carved height."))
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

	// Levels remap, applied first and ahead of Brightness/Contrast: t = (value - Min) / (Max -
	// Min), clamped to 0..1, then value = lerp(OutputMin, OutputMax, t). At the identity range
	// (0..1 in, 0..1 out) this is a no-op, which is what keeps every grade already authored
	// against Brightness/Contrast/Gamma unchanged.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade|Levels")
	float GradeInputMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade|Levels")
	float GradeInputMax = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade|Levels")
	float GradeOutputMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade|Levels")
	float GradeOutputMax = 1.0f;

	// Per-channel offset, added after the levels remap and the linear Brightness/Contrast stage
	// but ahead of the tonemap, so a colour cast can be dialled in on data the tonemap has not
	// yet reshaped. Zero on every channel is the identity.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade|Levels", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float GradeBiasR = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade|Levels", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float GradeBiasG = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade|Levels", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float GradeBiasB = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade")
	bool bGradeInvertMask = false;

	// Breakup. One tileable multi-scale SDF, built from three families of irregular cells and
	// spent four ways: relief inside the shape, a fold on its outside lip, a crease along the zero
	// crossing, and a push that warps the incoming height along the field gradient.
	//
	// It replaced Chipping in this slot. Chipping grew chips inward over N full-resolution
	// iterations against a thresholded height; this is two passes and no iteration, and it does
	// not need the surface to already contain raised material to find.

	// Macro cell count across one UV repeat. Mid and Detail are derived from this and Detail
	// rather than exposed, so the three families stay in a sensible ratio instead of being three
	// sliders an artist has to keep in step by hand.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (ClampMin = "1", UIMax = "64"))
	int32 BreakupScale = 6;

	// How far apart the derived Mid and Detail families sit from Macro. Low keeps all three near
	// the same size and the result reads as one population; high spreads them and gives large
	// plates broken by much finer fragments.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BreakupDetail = 0.5f;

	// Fraction of cells that are present at all. Below 1 the field has genuine gaps, which is what
	// separates scattered flakes from continuous plating.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BreakupDensity = 0.72f;

	// Piece size as a fraction of its own cell, so it tracks Scale instead of fighting it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (ClampMin = "0.001", UIMax = "1.0"))
	float BreakupSize = 0.32f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Advanced", meta = (ClampMin = "0.0", UIMax = "1.0"))
	float BreakupSizeVariation = 0.3125f;

	// Maximum aspect ratio a piece may be drawn at. Applied in both directions, so a single
	// control gives both elongated and squat fragments rather than a directional bias.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (ClampMin = "0.05", UIMax = "4.0"))
	float BreakupStretch = 1.6f;

	// Blends each piece from a box metric toward a diamond one: rounded flakes at 0, angular
	// shards at 1. A shape control rather than a second noise, so it changes what a fragment is
	// instead of where it sits.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BreakupAngularity = 0.72f;

	// How far a piece may wander off its lattice cell.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BreakupIrregularity = 0.38f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Advanced")
	EMixtormatBreakupOperation BreakupMidOperation = EMixtormatBreakupOperation::Union;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Advanced")
	EMixtormatBreakupOperation BreakupDetailOperation = EMixtormatBreakupOperation::Union;

	// Blend radius of the CSG operations, as a fraction of a piece. 0 is a hard boolean.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Advanced", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BreakupSmoothness = 0.30f;

	// Signed offset of the final field in reference pixels. Positive shrinks pieces and opens
	// space between them; negative grows them before relief is applied.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (ClampMin = "-64.0", UIMax = "64"))
	float BreakupInset = 0.0f;

	// Warps the whole field before the cells are evaluated, in reference pixels at 1K. Built from
	// integer-period sinusoids so the result still closes on the UV square exactly.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "32.0"))
	float BreakupDistortion = 5.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Advanced", meta = (ClampMin = "1", ClampMax = "16"))
	int32 BreakupDistortionFrequency = 3;

	// Signed, and the main structural control. Negative carves the pieces into the surface --
	// torn, flaked, recessed. Zero leaves the surface alone and lets Fold and Crease do the work.
	// Positive stands them proud, for rock foundations and raised plates.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (ClampMin = "-0.5", UIMax = "0.5"))
	float BreakupRelief = -0.06f;

	// Dedicated per-piece relief scaling. Kept separate from Variation so plate/flake thickness can
	// change without making crease, fold and push equally noisy.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BreakupThicknessVariation = 0.30f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (ClampMin = "0.0", UIMax = "64"))
	float BreakupGapWidth = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (ClampMin = "0.0", UIMax = "0.5"))
	float BreakupGapDepth = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BreakupGapVariation = 0.35f;

	// Raises the material just outside each piece. The lip of torn paper, peeling paint, curled
	// mud or a lifting ice plate.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (ClampMin = "0.0", UIMax = "0.5"))
	float BreakupFold = 0.025f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Advanced", meta = (ClampMin = "0.001", UIMax = "64"))
	float BreakupFoldWidth = 16.0f;

	// Cuts a narrow depression along the zero crossing itself: cracks, plate separation, torn
	// seams, rock joints. Works with Relief at 0, which is the crack-only configuration.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (ClampMin = "0.0", UIMax = "0.5"))
	float BreakupCrease = 0.018f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Advanced", meta = (ClampMin = "0.001", UIMax = "32"))
	float BreakupCreaseWidth = 1.25f;

	// Warps the incoming height along the field gradient instead of replacing it: compressed
	// material, pushed rock, bulging, warped strata. In reference pixels at 1K, so the visual
	// scale holds between a preview and a 4K bake.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (ClampMin = "-128.0", UIMax = "128"))
	float BreakupPush = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Advanced", meta = (ClampMin = "0.001", UIMax = "64"))
	float BreakupPushWidth = 24.0f;

	// Height separation added by Push. This makes Push visible even when the incoming height is flat;
	// the existing Push value still controls the signed UV advection distance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (ClampMin = "0.0", UIMax = "0.5"))
	float BreakupPushRelief = 0.035f;

	// Per-piece variation of relief, fold, crease and push, off the field's own stable piece id.
	// It is piece-stable by construction and can never become per-pixel noise.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BreakupVariation = 0.25f;

	// 0 is an exact rendering identity, and the passes are skipped entirely rather than run to
	// reproduce their input.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BreakupAmount = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float BreakupRoughnessAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Shading", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float BreakupNormalStrength = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Shading", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BreakupNormalSharpness = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Shading", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BreakupAOAmount = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Shading", meta = (ClampMin = "1.0", UIMax = "32.0"))
	float BreakupAORadius = 8.0f;

	// Flips the sign of the field, swapping which side of every boundary is the piece.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup")
	bool bBreakupInvert = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (ClampMin = "0"))
	int32 BreakupSeed = 1;

	// Optional mask owned by Breakup. When unset, the layer's accumulated mask children are used.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Placement")
	TSoftObjectPtr<UMixtormatMask> BreakupMask;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Placement")
	TSoftObjectPtr<UTexture2D> BreakupMaskTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Placement", meta = (ClampMin = "1"))
	int32 BreakupMaskTiling = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Placement")
	bool bBreakupInvertMask = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges")
	int32 EdgeWearRadius = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges")
	float EdgeWearSlope = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges")
	float EdgeWearStrength = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges")
	float EdgeWearFeather = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges")
	int32 EdgeWearDirections = 24;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges")
	float EdgeWearAngularAA = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges")
	float EdgeWearGravity = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges")
	float EdgeWearGravityAngle = -90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges")
	int32 EdgeWearSeed = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges")
	int32 EdgeWearMacroScale = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges")
	float EdgeWearMacroAmount = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges")
	int32 EdgeWearCellScale = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges")
	float EdgeWearCellAmount = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges")
	int32 EdgeWearRidgeScale = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges")
	float EdgeWearRidgeAmount = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges")
	int32 EdgeWearMicroScale = 40;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges")
	float EdgeWearMicroAmount = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges")
	int32 EdgeWearWarpScale = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges")
	float EdgeWearWarpAmount = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges")
	float EdgeWearNoiseContrast = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges")
	float EdgeWearIdVariation = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges")
	float EdgeWearIdRadius = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges")
	float EdgeWearIdSlope = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges")
	float EdgeWearIdStrength = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges")
	float EdgeWearIdNoise = 1.0f;

	// Roughness is applied only through the generated wear coverage. Weight is the output
	// enable/strength control; Offset is signed so worn edges may become rougher or smoother.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges|Output")
	float EdgeWearRoughnessWeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges|Output")
	float EdgeWearRoughnessOffset = 0.0f;

	// Tileable curl-flow distortion applied to every composited material channel together.
	// Amount is signed: reversing it follows the same field in the opposite direction.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow Warp")
	float FlowWarpAmount = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow Warp", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FlowWarpWeight = 1.0f;

	// Integer cells per UV repeat keep the generated vector field seamless.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow Warp", meta = (ClampMin = "1"))
	int32 FlowWarpScale = 8;

	// Rotates the curl vectors without rotating their periodic sampling lattice.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow Warp")
	float FlowWarpDirection = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow Warp", meta = (ClampMin = "0"))
	int32 FlowWarpSeed = 1;

	// Scoped-mask and current-height gradients steer the curl downhill. Zero preserves curl V1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow Warp|Slope", meta = (ClampMin = "0.0"))
	float FlowWarpMaskSlopeInfluence = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow Warp|Slope", meta = (ClampMin = "0.0"))
	float FlowWarpHeightSlopeInfluence = 0.0f;

	// Pixel radii for the wrapped derivative kernel. Larger values reject finer slope detail.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow Warp|Slope", meta = (ClampMin = "1.0"))
	float FlowWarpDerivativeKernelX = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow Warp|Slope", meta = (ClampMin = "1.0"))
	float FlowWarpDerivativeKernelY = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow Warp|Output")
	EMixtormatFlowWarpBlendMode FlowWarpBlendMode = EMixtormatFlowWarpBlendMode::Replace;

	// ---- Layer Blur ------------------------------------------------------------------
	// Per axis, like the mask blur, and for the same reasons: the shader runs a dispatch per
	// direction so a zero radius costs nothing, an unequal pair is anisotropic, and each axis
	// can be driven on its own where a direction enum could not be driven at all.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Blur", meta = (DisplayName = "Radius X", ClampMin = "0.0", ClampMax = "32.0"))
	float LayerBlurRadiusX = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Blur", meta = (DisplayName = "Radius Y", ClampMin = "0.0", ClampMax = "32.0"))
	float LayerBlurRadiusY = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Blur")
	EMixtormatLayerBlurScope LayerBlurScope = EMixtormatLayerBlurScope::Layer;

	// Lerped against the unblurred source, so 0 is the identity and the pass is skipped
	// outright rather than paying for a dispatch that reproduces its own input.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Blur", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LayerBlurAmount = 1.0f;

	// Height last, and optional, because it is the one channel where softening changes what
	// the surface *is* rather than how it looks: the height feeds displacement, the height
	// blend between layers, and the normals derived from it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Blur", meta = (DisplayName = "Blur Height"))
	bool bLayerBlurHeight = true;
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
	int32 Scale = 80;

	// 0 puts the cells on a regular lattice and gives grout: brick, tile, plank. 1 gives organic
	// crazing. Also one control from two -- Lattice jittered its cell points and Propagated its
	// nuclei, and hiding the lattice is the same intent either way.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Jitter = 1.0f;

	// In cell units, so it means the same thing at any Scale.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Width = 0.05f;

	// Thins individual cracks, so the network reads as breaks that opened at different times
	// rather than as a uniform lattice. Keyed on the whole crack, not on either side of it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Variation = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure", meta = (ClampMin = "0"))
	int32 Seed = 1;

	// -- Warp ---------------------------------------------------------------------------------
	// Bends the finished network rather than steering how it grows, so it costs a resolve pass and
	// rebuilds nothing. Periodic curl noise: divergence-free, and it wraps on its own period, so
	// the result still tiles.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Warp = 0.25f;

	// Size of the warp's swirls, in repeats across the UV. Its seed comes off Seed, so reseeding
	// the network reseeds the warp with it rather than leaving a second seed to remember.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure", meta = (ClampMin = "1", ClampMax = "32"))
	int32 WarpScale = 32;

	// -- Growth (Propagated mode) ---------------------------------------------------------------
	// Ignored in Lattice mode, which needs none of it: a Voronoi diagram has no growth to steer.

	// How far a crack can reach, in pixels at a 1024 reference, scaled by the render resolution so
	// a preview and an export grow the same network rather than the same pixel count. The dispatch
	// count scales with it, so this is the control that costs.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Growth", meta = (ClampMin = "1", ClampMax = "1024"))
	int32 Iterations = 32;

	// Fraction of cells that get a nucleus at all: how many separate cracks there are, as opposed
	// to how far each one runs.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Growth", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Density = 0.5f;

	// Size of the stress, toughness and flow fields as a multiple of Scale, rather than as a
	// second cell count.
	//
	// It used to be NoiseCells, an absolute number that fought Scale: moving either changed the
	// character, because what decides it is the field's size *relative* to the pieces. Under 1 the
	// fields steer whole regions; over 1 they roughen individual cracks.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Growth", meta = (ClampMin = "0.1", ClampMax = "8.0"))
	float Detail = 4.0f;

	// How far the stress and toughness fields depart from uniform. At 0 the network is steered
	// only by flow and roughness, which reads as combed rather than as fractured.
	//
	// One dial where there were two. Stress driving cracking on and toughness holding it back are
	// two ends of one balance, they read from independent noise either way, and a uniform stress
	// field over a varying toughness one is not a thing anyone reached for.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Growth", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FieldContrast = 0.2f;

	// How strongly those fields win against a tip's own heading. Was StressGain and ToughnessCost:
	// two weights on opposite signs of the same comparison.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Growth", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float FractureBias = 0.5f;

	// How much a crack is a line rather than a blob. Was Persistence, weighting a tip's preference
	// to carry straight on, and TurnResponse, setting how fast its stored heading caught up -- how
	// hard it resists turning and what happens when it does. They only ever moved together.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Growth", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Straightness = 1.0f;

	// Coherent wander: how much the curl-flow field bends a running crack. At 1 cracks follow the
	// field and come out combed.
	//
	// Deliberately not merged with Roughness. Flow is directional and continuous, Roughness is
	// per-step noise; combed and ragged are different looks and one dial cannot do both.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Growth", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Flow = 0.25f;

	// Incoherent wander: random jitter in the choice of next step. Roughens a crack's edge without
	// giving it anywhere to go.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Growth", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float Roughness = 0.1f;

	// -- Advanced -------------------------------------------------------------------------------
	// Growth-kernel internals, right for almost everything. Here because between them they decide
	// whether the network terminates like a drying film or keeps running.

	// Score a step has to beat before a tip advances at all.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Advanced", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float GrowthThreshold = 0.0f;

	// How many existing cracks a tip may touch before it stops. This is what makes the network meet
	// at right angles like a drying film instead of at the 120 degrees a bisector diagram gives,
	// and it is the whole reason the propagated mode exists.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Advanced", meta = (ClampMin = "1", ClampMax = "8"))
	int32 CollisionLimit = 4;

	// -- Relief -----------------------------------------------------------------------------------
	// The generated groove is authoritative for both height and its derived normal.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Relief", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReliefDepth = 0.25f;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Craquelure normals derive from Relief Depth."))
	float ReliefNormalStrength = 1.0f;

	// The groove's mouth, and the curve of its wall between a straight V and a rounded U. Not
	// merged: a wide V and a narrow U are both things you would ask for.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Relief", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReliefWidth = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Relief", meta = (ClampMin = "0.05", ClampMax = "8.0"))
	float ReliefProfile = 2.0f;

	// Per-crack variation on the relief, keyed off the same lineage id the crack network already
	// carries -- stable per crack rather than noisy per pixel, and applied after the network
	// exists so it never touches which cracks grow or where. Each is its own multiplier on top
	// of Height/Groove/Profile above and is neutral at 0, so an authored relief looks the same
	// until one of these is raised.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Relief", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReliefGrooveVariation = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Relief", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReliefProfileVariation = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Relief", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReliefWidthVariation = 0.0f;

	// -- Output -----------------------------------------------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Blend")
	EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::Replace;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Blend", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Weight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craquelure|Blend")
	FMixtormatMaskShaping Shaping;

	FMixtormatCraquelure()
	{
		Shaping.bInvert = true;
	}
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color ID", meta = (ClampMin = "0.0", UIMax = "0.5"))
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
// What a cluster filter segments.
//
// Two genuinely different questions, and which one is right depends on what the regions are for.
// Layer Surface finds the structure in this layer's own scan -- the bricks in the brick texture --
// regardless of what is under it. Composite Below finds the structure in the surface actually
// accumulated beneath this layer, so the regions follow what is visible there, including whatever
// every layer under this one contributed.
//
// It is also the noise control. A raw scan can be busy enough that the segmentation shatters into
// far more regions than the eye reads as pieces; the composited surface below has usually been
// through blending, height and grading and comes out calmer. Neither is the right answer in
// general, which is why this is a switch and not a default.
UENUM(BlueprintType)
enum class EMixtormatClusterSource : uint8
{
	LayerSurface UMETA(DisplayName = "Layer Surface"),
	CompositeBelow UMETA(DisplayName = "Composite Below")
};

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatClusterFilter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster IDs")
	bool bEnabled = true;

	// On the bottom layer there is nothing composited below, so Composite Below has only the flat
	// substrate to look at and would return a single region covering everything. It falls back to
	// the layer's own surface there rather than silently producing nothing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster IDs")
	EMixtormatClusterSource Source = EMixtormatClusterSource::LayerSurface;

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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cluster IDs", meta = (ClampMin = "0.0", UIMax = "4.0"))
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
	float SaturationMin = 0.95f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HSV From IDs|Jitter", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float SaturationMax = 1.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HSV From IDs|Jitter", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float ValueMin = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HSV From IDs|Jitter", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float ValueMax = 1.0f;

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
UENUM(BlueprintType)
enum class EMixtormatIdCombineMode : uint8
{
	// A pair of neighbouring regions draws once between them and joins if it falls under Amount.
	// Every boundary in the map is a candidate, so the result reads as a coarser version of the
	// same pattern -- the shapes stay in family, there are simply fewer and bigger ones.
	Merge UMETA(DisplayName = "Merge"),
	// Whole regions are drawn instead, and a chosen one dissolves into whatever it borders. The
	// survivors keep their original outline exactly, so this reads as pieces being taken out of
	// the map rather than the whole map being coarsened.
	Subtract UMETA(DisplayName = "Subtract")
};

// Fewer, larger regions, from the ID map above this node.
//
// Sits between an ID creator and whatever consumes it -- a cluster filter or a pattern upstream,
// an HSV tint, random values, a ramp or a peel downstream -- and rewrites the map in place. It is
// the only ID node that both reads and writes one, which is what makes it stackable: several in a
// row keep coarsening, and everything downstream still just sees an ID map.
//
// Merges neighbours, never arbitrary pairs. Merging by hash bucket alone would give two regions
// on opposite sides of the surface the same number without either becoming larger; joining
// neighbours grows genuinely bigger contiguous cells, which is what an unsubdivide has to do if
// the result is going to drive colour variation or relief.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatCombineIdFilter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combine IDs")
	bool bEnabled = true;

	// Chance that any one candidate takes. 0 passes the map through untouched, 1 collapses every
	// region that touches another into a single one.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combine IDs", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Amount = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combine IDs", meta = (ClampMin = "0"))
	int32 Seed = 0;

	// How many rounds of merging run, which is the unsubdivide depth.
	//
	// Each round draws with its own salt and tests the regions the previous round produced, so
	// raising it keeps coarsening instead of re-deciding the same pairs. Two rounds at a low
	// Amount is a different picture from one round at a high one: the first grows clusters of
	// clusters, the second grows one big one.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combine IDs", meta = (ClampMin = "1", ClampMax = "8"))
	int32 Passes = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combine IDs")
	EMixtormatIdCombineMode Mode = EMixtormatIdCombineMode::Merge;
};

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

// Serialized by value. Append new modes; never reorder or insert between existing entries.
UENUM(BlueprintType)
enum class EMixtormatPatternMode : uint8
{
	Grid = 0 UMETA(DisplayName = "Grid"),
	RunningBond = 1 UMETA(DisplayName = "Running Bond"),
	Herringbone = 2 UMETA(DisplayName = "Herringbone"),
	Basketweave = 3 UMETA(DisplayName = "Basketweave"),
	Hex = 4 UMETA(DisplayName = "Hex"),
	OctagonSquare = 5 UMETA(DisplayName = "Octagon + Square"),
	Flagstone = 6 UMETA(DisplayName = "Flagstone"),
	Voronoi = 7 UMETA(DisplayName = "Voronoi"),
	Hopscotch = 8 UMETA(DisplayName = "Hopscotch"),
	FrenchAshlar = 9 UMETA(DisplayName = "French / Modular Ashlar"),
	FracturePlates = 10 UMETA(DisplayName = "Fracture Plates")
};

UENUM(BlueprintType)
enum class EMixtormatGridMode : uint8
{
	Straight = 0 UMETA(DisplayName = "Straight"),
	Staggered = 1 UMETA(DisplayName = "Staggered"),
	Diamond = 2 UMETA(DisplayName = "Diamond / 45 Degree")
};

// Procedural region producer. A periodic rectangular lattice and the jittered Voronoi end of the
// same solver publish the same sparse pixel-index IDs as Cluster IDs, so HSV/Random/Ramp From IDs
// consume either producer without a special path.
//
// Rows/Columns/RowOffset/Jitter are the pattern vocabulary. The relief and UV blocks are optional
// intrinsic consumers of the same regions: zero relief leaves height/normal/RAM untouched, and UV
// variation off leaves the layer's source mapping untouched.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatPatternFilter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Lattice")
	EMixtormatPatternMode PatternMode = EMixtormatPatternMode::Grid;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Lattice")
	EMixtormatGridMode GridMode = EMixtormatGridMode::Straight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Lattice", meta = (ClampMin = "1", ClampMax = "256"))
	int32 Rows = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Lattice", meta = (ClampMin = "1", ClampMax = "256"))
	int32 Columns = 8;

	// Fraction of one cell shifted per row. The shader quantises this just enough for the final row
	// to meet the first row periodically; 0.5 is running bond when the row count closes on it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Lattice", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RowOffset = 0.0f;

	// 0 keeps feature points at cell centres; 1 gives the full periodic Voronoi solve.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Lattice", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Jitter = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Lattice")
	bool bSwapAxes = false;

	// Rounds the cell corners by blending the two nearest walls instead of taking a hard minimum,
	// so a chamfer run off the edge distance fillets into the corner instead of creasing. In cell
	// fractions, so it means the same thing on a large lattice and a small one. 0 is the true
	// Voronoi corner.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Lattice", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
	float Rounding = 0.0f;

	// Whether Edge Width and the chamfer are measured as a fraction of the cell rather than in
	// output pixels. Relative frames every cell the same way whatever its size or aspect;
	// absolute keeps an even visual width across cells of different sizes.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Edges")
	bool bRelativeEdgeWidth = false;

	// Region-less grout. Consumers already treat MIXTORMAT_INVALID_REGION as pass-through/no mask.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Lattice", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "64.0"))
	float GapPixels = 1.0f;

	// Where the grout sits relative to the cells. Negative sinks it into a trench, positive stands
	// it proud as a raised mortar line. Inert at Gap 0, since there is then nothing outside the
	// IDs to move.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Lattice", meta = (UIMin = "-1.0", UIMax = "1.0"))
	float GapHeight = -1.0f;

	// Varies the grout once per piece rather than once per surface, for every Pattern Mode.
	//
	// Cut per piece, not per wall: the wall stays exactly where the topology put it and each side
	// of it pulls back by its own draw, so the visible grout between two pieces is the sum of two
	// independent amounts. A per-wall gap would need a neighbour id, which only the cellular modes
	// can produce; this asks nothing of the mode beyond the edge distance every one of them
	// already publishes. Symmetric about Gap, the same convention as the bevel's Variation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Lattice", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GapRandom = 0.0f;

	// Spends that same grout unevenly around the piece instead of ringing it: the piece sits off
	// centre in its own socket, hard against one wall with the space behind it. Bounded by the
	// piece's own half-gap, so a piece can never cross into its neighbour -- which is also why
	// both of these are inert at Gap 0, where there is no space to sit off centre in.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Lattice", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GapSlide = 0.0f;

	// Per-region source UV variation. Pattern is the first producer that knows an analytic centre,
	// so this stays Pattern-only until Cluster IDs exposes equivalent region bounds/centres.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|UV")
	bool bUVVariation = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|UV")
	bool bOrthogonalUV = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|UV", meta = (ClampMin = "-360.0", ClampMax = "360.0"))
	float UVRotationMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|UV", meta = (ClampMin = "-360.0", ClampMax = "360.0"))
	float UVRotationMax = 360.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|UV", meta = (ClampMin = "0.05", ClampMax = "8.0"))
	float UVScaleMin = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|UV", meta = (ClampMin = "0.05", ClampMax = "8.0"))
	float UVScaleMax = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|UV", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float UVOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|UV")
	bool bRandomFlipU = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|UV")
	bool bRandomFlipV = false;

	// Per-cell elevation, not per-cell tilt: every cell is a flat face sitting somewhere off the
	// base. Tilting a region is Ramp From IDs' job, and that filter composites over this one --
	// chamfer and settle here, slope there.
	//
	// Height is the elevation every cell gets; Height Random is how far below it a cell may be
	// drawn, as a multiplier on Height. One-sided on purpose: a cell that went below the base
	// would feather back up at its own wall and read as a recessed panel in a raised frame.
	// At Height Random 0 every cell sits at full Height; at 1 they spread down to the base.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Relief", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
	float HeightAmount = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Relief", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HeightRandom = 0.5f;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Pattern normals derive from the final pattern height."))
	float NormalStrength = 4.0f;

	// The chamfer's cross-section, from the grout line up to the flat of the cell. -1 is a cove
	// that hugs the grout then sweeps up into the face, 0 a straight flat chamfer, +1 a bullnose
	// that lifts away and rounds over onto the face. Both ends of the curve stay pinned, so this
	// changes the chamfer's shape and never its width or height.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Relief", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Profile = 0.25f;

	// Offsets the roundness per cell rather than scaling it, so one cell's bullnose can be its
	// neighbour's cove -- which scaling a signed control could never produce.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Relief", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ProfileRandom = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Relief", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "0.5"))
	float Feather = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Relief", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FeatherRandom = 0.5f;

	// What the feather does on the way up, rather than how wide it is.
	//
	// The run-out is a straight line, and a straight line is the one shape a normal map cannot
	// show -- a normal reads a change in slope, and a constant ramp has none, so the band lights
	// as a single flat facet. Gain bends it: the slope leaving the wall goes from 1 to 1 + Gain,
	// and past 1 the curve arcs above the face and leaves a raised lip just inside the edge. Both
	// ends stay pinned, so the grout wall and the flat face never move.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Relief", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "4.0"))
	float FeatherGain = 0.0f;

	// Edge relief and shading share one edge-distance field. Width is in output pixels, so the
	// bevel remains visually even when Rows and Columns make non-square cells.
	//
	// Signed, and it lifts the cell *face*: positive stands the cell proud of the grout with the
	// chamfer ramping down to it, negative sinks the face below the grout instead. The gap itself
	// is untouched either way -- that is Gap Height's job, so the two compose rather than fight.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Edges", meta = (UIMin = "-1.0", UIMax = "1.0"))
	float BevelHeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Edges", meta = (ClampMin = "0.0001", UIMin = "0.25", UIMax = "64.0"))
	float BevelWidthPixels = 4.0f;

	// The same width for Relative mode, as a fraction of the way from the cell wall to its
	// deepest interior point. Separate from the pixel value because the two need different
	// ranges to be draggable at all.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Edges", meta = (ClampMin = "0.0001", UIMin = "0.0", UIMax = "1.0"))
	float BevelWidthCells = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Edges", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BevelVariation = 0.0f;

	// Slides the chamfer band across the grout line, in output pixels. Negative puts it out in
	// the gap, positive pulls it onto the cell face, zero starts it exactly at the gap wall.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Edges", meta = (UIMin = "-32.0", UIMax = "32.0"))
	float BevelInsetPixels = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Edges", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EdgeRoughness = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Edges", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EdgeRoughnessAmount = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Edges", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AOAmount = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Edges", meta = (ClampMin = "1.0", ClampMax = "8.0"))
	float AOSpread = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs", meta = (ClampMin = "0"))
	int32 Seed = 1;

	// ---------------------------------------------------------------------------------------
	// Fracture Plates. Read only when PatternMode is FracturePlates; every other mode ignores
	// them, so these are inert on an existing asset.
	//
	// The mode's primary lattice is the shared Columns/Rows pair above, its seed placement is the
	// shared Jitter, and its shuffle is the shared Seed -- it is a Pattern topology like every
	// other one, not a second pattern system bolted alongside.
	// ---------------------------------------------------------------------------------------

	// Spread of the additive power weights that decide how much territory a plate wins from its
	// neighbours. This is where "some very large regions, some small" comes from: at 0 every plate
	// is the same importance and the result is pavement, and raising it grows a few plates at the
	// expense of the rest. Additive rather than multiplicative on purpose -- an additive weight
	// moves a boundary while leaving it straight, a multiplicative one bows it into an arc.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Fracture", meta = (UIMin = "0.0", UIMax = "1.0"))
	float FractureSizeVariation = 0.3f;

	// Probability that a primary plate fractures internally at all. A plate that does not stays
	// whole and publishes one ID, so this is the control for how much of the surface reads as
	// large unbroken pieces against locally shattered ones.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Fracture", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FractureSecondaryAmount = 0.5f;

	// How many pieces a fracturing plate breaks into, drawn per plate. Both ends are structural
	// indices rather than artistic amounts, so they are range-clamped: below 2 is not a fracture.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Fracture", meta = (ClampMin = "2", ClampMax = "8"))
	int32 FractureSecondaryMin = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Fracture", meta = (ClampMin = "2", ClampMax = "8"))
	int32 FractureSecondaryMax = 3;

	// How far the secondary sites sit from their parent's, in cell fractions. Small values put
	// the split near the middle of the plate; large ones push the pieces toward its walls.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Fracture", meta = (ClampMin = "0.0", UIMin = "0.05", UIMax = "0.75"))
	float FractureSecondaryRadius = 0.34f;

	// Breaks up the even ring the secondary sites are laid on, in angle and in radius, so a split
	// plate does not come out as a regular pie.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Fracture", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
	float FractureSecondaryJitter = 0.55f;

	// How far, in output pixels, the fracture field displaces a plate wall from the straight line
	// the power diagram would give it. The displacement field is piecewise planar, so the wall
	// stays a chain of straight runs meeting at angles rather than becoming a curve.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Fracture", meta = (UIMin = "0.0", UIMax = "32.0"))
	float FractureEdgeIrregularity = 8.0f;

	// The run length of those straight segments, in output pixels. Absolute: Columns and Rows do
	// not stretch it, so changing the plate count leaves the crack character alone.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Fracture", meta = (ClampMin = "1.0", UIMin = "8.0", UIMax = "512.0"))
	float FractureEdgeScale = 96.0f;

	// A second, shorter octave of the same field: the small branching kinks that sit on the long
	// primary fracture runs. Also absolute, in output pixels.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Fracture", meta = (UIMin = "0.0", UIMax = "16.0"))
	float FractureEdgeDetail = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pattern IDs|Fracture", meta = (ClampMin = "1.0", UIMin = "4.0", UIMax = "128.0"))
	float FractureEdgeDetailScale = 24.0f;
};

// Per-region tilt from a gradient. Gives every region its own local frame, runs a linear ramp
// across it at a random angle, and uses that to lift one side of the region and sink the other in
// the composited height and normal.
//
// This is what stops tiles, cobbles and shards lying in the same plane. Overlaying a noise varies
// pixels; this varies pieces, so the surface reads as individually settled fragments rather than
// one flat sheet with a texture on it.
//
// A Filter by category and by menu, but it owns a pass that runs *after* the layer composites --
// the same shape as craquelure, which builds its network before the composite and defers its
// relief until the height it is carving actually exists. Deliberately not an Effect: effects here
// are asset-backed, picked from a registry, and there is no asset a per-region tilt points at.
//
// The local frame is recovered from the ID itself: an ID is the linear index of its region's root
// pixel, so the root's position comes for free and every bounding box is measured relative to it.
// That is why nothing compacts these IDs to a dense range -- doing so would destroy the one thing
// this node needs from them.
//
// Reads the nearest enabled cluster filter above it in the child list. With none, it does
// nothing.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatRampIdFilter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramp From IDs")
	bool bEnabled = true;

	// -- Relief ---------------------------------------------------------------------------------
	// The composited ramp height is authoritative for its derived normal.

	// How strongly each region's ramp meets the surface. A blend weight, not a tilt amount: at 0
	// the surface is untouched under every blend mode, not only the additive ones.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramp From IDs|Relief", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "0.5"))
	float HeightAmount = 0.05f;

	// Per-region jitter on that strength. Base plus jitter rather than range times amount, unlike
	// Pattern IDs' Height pair: a uniform ramp strength is meaningful on its own -- every region
	// tilts equally, each in its own direction -- so 0 here leaves every region at full strength
	// rather than switching the node off.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramp From IDs|Relief", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float IntensityRandom = 0.0f;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Ramp normals derive from Intensity."))
	float NormalStrength = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramp From IDs|Relief", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AOAmount = 0.0f;

	// How the ramp meets the height under it. AddSub is the centred case this node used to
	// hard-code, and is the default so existing materials keep their look.
	//
	// The normal is derived from the blended height rather than blended separately, so it always
	// describes the surface that was actually written -- under Min and Multiply a separately
	// blended normal would not.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramp From IDs|Relief")
	EMixtormatMaskBlendMode BlendMode = EMixtormatMaskBlendMode::AddSub;

	// -- Gradient -------------------------------------------------------------------------------

	// Whether each region's gradient runs in its own random direction. Off puts every gradient on
	// the same axis, which reads as a comb rather than as settling.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramp From IDs|Gradient")
	bool bRotateRandom = true;

	// Quantises each region's angle, so a lattice reads as deliberately laid rather than
	// scattered. Applied to the true screen-space angle, which is the only place a degree step
	// means what it says.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramp From IDs|Gradient")
	bool bAngleStepping = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramp From IDs|Gradient", meta = (ClampMin = "0.01", UIMin = "1.0", UIMax = "90.0"))
	float AngleStepDegrees = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ramp From IDs", meta = (ClampMin = "0"))
	int32 Seed = 1;
};

// Which generator an FMixtormatGenerator carries.
//
// Serialised by value on the child. Append only -- a new generator takes the next number and
// brings its own payload struct; nothing existing moves.
UENUM(BlueprintType)
enum class EMixtormatGeneratorType : uint8
{
	StrataCarver UMETA(DisplayName = "Strata Carver")
};

// Strata Carver: sedimentary/weathered carving driven by a propagated distance solve.
//
// It modifies the layer's input height. It is not a mask generator that happens to be wired to
// height -- the final height is
//
//     CarvedHeight = SourceHeight - CarveMask * Depth * Influence
//
// with SourceHeight taken exactly as authored. Nothing normalises the incoming height before
// the carve, because a wood plank whose height sits in 0.4..0.6 and a rock whose height covers
// 0..1 must come out with the same *absolute* groove depth for one Depth value; normalising
// first would make Depth mean "a fraction of whatever contrast this map happened to have".
//
// The solver is a recursive distance propagation over a Worley-seeded field, ported in concept
// from the Houdini/OpenCL prototype. Its raw recursive distance is kept separate from display
// remapping throughout: Bias, the two remaps and the clamp are applied once at the very end, so
// scrubbing them reshapes a finished field instead of changing what propagated.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatStrataCarver
{
	GENERATED_BODY()

	// ---- Main ----

	// Drives both the internal fractal seed and every per-iteration draw the solver makes.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (ClampMin = "0"))
	int32 Seed = 3;

	// How far the carve cuts, in the same units as the layer's height. Subtracted, never added:
	// a carver removes material.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Depth = 0.05f;

	// Propagation steps. The jump schedule halves its stride inside this budget rather than
	// consuming one iteration per texel, so a high count buys depth of recursion, not reach.
	//
	// 1..64 everywhere -- the data model, the gather clamp and the inspector slider all agree,
	// so the stored value and the one the slider can reach are the same number. 64 is the
	// default because it is where the picture stops changing on every surface this was authored
	// against, and nothing in the solver is keyed to the count: the jump schedule is a function
	// of JumpStart alone, so raising the ceiling later is a one-line change here and in the
	// gather, not an algorithm change.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (ClampMin = "1", ClampMax = "64"))
	int32 Iterations = 64;

	// Where the seed field is cut into "carved" and "not carved". Higher leaves fewer, more
	// isolated origins; lower floods the surface.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SeedThreshold = 0.25f;

	// Feature size of the internal seed, as cells across the tile. Drives Worley Cells: the
	// artist-facing dial is one number, and the solver's cell count follows it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (ClampMin = "1", ClampMax = "64"))
	int32 Scale = 3;

	// Octaves of the internal fractal seed. Broad natural noise rather than pixel noise is the
	// whole point of the default: a seed with detail turned up reads as dirt, not as strata.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (ClampMin = "1", ClampMax = "8"))
	int32 SeedDetail = 3;

	// The banding. Frequency is how many strata cross the field, Amount how hard they bite into
	// the propagated distance, Warp how far the bands wander off straight.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (ClampMin = "0.0", ClampMax = "64.0"))
	float StrataFrequency = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (ClampMin = "0.0", ClampMax = "16.0"))
	float StrataAmount = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float StrataWarp = 0.54f;

	// How hard a propagating front shoves its own strata phase into the next step. This is the
	// recursion that makes the result read as layered rock rather than as a distance field.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float PushAmount = 0.5f;

	// 0 ignores any mask scoped under this generator entirely. 1 lets it decide where carving
	// starts, how cheaply it spreads and how deep it cuts.
	//
	// Not a final multiply. A mask applied only at the end gives a hard cutout with full-strength
	// carving inside it; feeding seed probability and propagation cost as well is what produces
	// localised weathering that fades at its own edges.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaskInfluence = 1.0f;

	// How much Region IDs above this generator vary the carve. Exactly zero at 0 -- the shader
	// branches rather than multiplying by zero, so a stack with no ID producer and a stack with
	// one at influence 0 are bit-identical.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float IDInfluence = 0.0f;

	// ---- Advanced ----

	// Distance added per propagation step, before cost. The solver's speed dial.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver|Advanced", meta = (ClampMin = "0.001", ClampMax = "4.0"))
	float StepScale = 0.3f;

	// Widest jump stride, in texels at the reference resolution. The schedule halves from here
	// to one, so this sets how far a front can reach in its first pass rather than how many
	// passes run.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver|Advanced", meta = (ClampMin = "1", ClampMax = "256"))
	int32 JumpStart = 24;

	// The unreachable distance. Anything still holding this when the solve ends never had a
	// front arrive, and reads as uncarved.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver|Advanced", meta = (ClampMin = "1.0"))
	float MaxValue = 256.0f;

	// Worley feature-point jitter. 0 puts the points on the lattice and the strata come out
	// regular; 1 is full Voronoi.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver|Advanced", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WorleyJitter = 1.0f;

	// Frequency of the banded/ringed Worley family, which is the one that reads as bedding
	// planes rather than as cells.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver|Advanced", meta = (ClampMin = "0.0", ClampMax = "16.0"))
	float BandFrequency = 1.0f;

	// How much the seed field resists propagation. High cost makes fronts hug the cheap
	// channels and the carve comes out as veins; low cost floods.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver|Advanced", meta = (ClampMin = "0.0", ClampMax = "32.0"))
	float CostAmount = 5.0f;

	// How fast a push dies out behind the front. 0 would carry one push across the whole tile.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver|Advanced", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PushDecay = 0.2f;

	// Reshuffles which operation and which Worley family each iteration picks, without changing
	// the seed field. The cheap dial: reseeding here re-solves, it does not re-seed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver|Advanced", meta = (ClampMin = "0"))
	int32 OperationSeed = 6;

	// Display shaping, all of it applied once after the solve. Bias is a gamma-style pull about
	// the midpoint; the two remaps and the clamp follow it in that order.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver|Advanced", meta = (ClampMin = "0.001", ClampMax = "1.0"))
	float Bias = 0.68f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver|Advanced", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RemapInMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver|Advanced", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RemapInMax = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver|Advanced", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RemapOutMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver|Advanced", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RemapOutMax = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver|Advanced", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ClampMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Strata Carver|Advanced", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ClampMax = 1.0f;
};

// One GENERATORS child, whatever kind it is.
//
// The wrapper exists so the category is one thing everywhere -- one child type, one owner type,
// one badge, one dispatch, one gather branch, one inspector slot -- and the kind is a field
// inside it rather than a second discriminator bolted onto EMixtormatLayerChildType. Adding a
// generator is then: a value on EMixtormatGeneratorType, a payload struct beside StrataCarver,
// a case in AddGeneratorPasses, and an inspector panel. Nothing that already exists changes.
//
// This is the opposite of the trade EMixtormatLayerChildType makes for filters, and deliberately
// so. Filters are discriminated at the top level because every dispatch in the plugin already
// switches on Type and a nested branch would be invisible to the compiler. Generators run in one
// place -- a single call before the composite -- so there is exactly one switch to keep honest,
// and the flat alternative would add a child-type value per generator forever.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatGenerator
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generator")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generator")
	EMixtormatGeneratorType Type = EMixtormatGeneratorType::StrataCarver;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generator", meta = (EditCondition = "Type == EMixtormatGeneratorType::StrataCarver"))
	FMixtormatStrataCarver StrataCarver;
};

// How a layer's base colour combines with what is composited below it.
//
// Separate from EMixtormatMaskBlendMode, and it has to be. That one operates on 0..1 coverage in
// the mask chain; this operates on colour at the composite. Half of what is here -- Screen, Soft
// Light, the dodge/burn pair, the four non-separable HSL transfers -- means nothing on coverage,
// and the two lists have different orders. Sharing one enum would put "Multiply" at two indices
// and invite exactly the mix-up that costs an afternoon.
//
// Grouped by what they do: arithmetic, then the contrast pairs, then the comparative ones, then
// the four that work on the colour as a whole. Serialised by value, so this order is fixed --
// anything new goes on the end.
UENUM(BlueprintType)
enum class EMixtormatColorBlendMode : uint8
{
	Normal UMETA(DisplayName = "Normal"),
	Add UMETA(DisplayName = "Add"),
	Subtract UMETA(DisplayName = "Subtract"),
	Multiply UMETA(DisplayName = "Multiply"),
	Divide UMETA(DisplayName = "Divide"),
	Screen UMETA(DisplayName = "Screen"),
	Overlay UMETA(DisplayName = "Overlay"),
	HardLight UMETA(DisplayName = "Hard Light"),
	SoftLight UMETA(DisplayName = "Soft Light"),
	ColorDodge UMETA(DisplayName = "Color Dodge"),
	ColorBurn UMETA(DisplayName = "Color Burn"),
	AddSub UMETA(DisplayName = "Add/Sub"),
	Difference UMETA(DisplayName = "Difference"),
	Exclusion UMETA(DisplayName = "Exclusion"),
	Min UMETA(DisplayName = "Min"),
	Max UMETA(DisplayName = "Max"),
	Hue UMETA(DisplayName = "Hue"),
	Saturation UMETA(DisplayName = "Saturation"),
	Color UMETA(DisplayName = "Color"),
	Luminosity UMETA(DisplayName = "Luminosity")
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
	RandomId UMETA(DisplayName = "Random From IDs"),
	RampId UMETA(DisplayName = "Ramp From IDs"),
	PatternId UMETA(DisplayName = "Pattern IDs"),
	// Scoped beneath the mask it blurs, never standing on its own in the chain. A node and
	// not a field on FMixtormatMaskLayer because only a node can be a driver destination,
	// a published source, or the one definition behind several instances.
	Blur UMETA(DisplayName = "Blur"),
	// Scoped the same way and for the same reasons. Only ever keeps: its result multiplies
	// into the coverage the mask already had, never widens it.
	Curvature UMETA(DisplayName = "Curvature"),
	// Appended, like everything below ColorId. Reads the ID map above it and republishes a
	// coarser one, so it is the only ID node that is neither a pure creator nor a pure consumer.
	CombineId UMETA(DisplayName = "Combine IDs"),
	// GENERATORS. Deliberately not Effect and deliberately not Generated -- those are the two
	// things this is most likely to be mistaken for, and it is neither.
	//
	// Generated is a *mask* producer: it emits 0..1 coverage and joins the mask chain. Effect is
	// a filter over the layer's finished composite -- Erosion, Breakup and Worn Edges all run
	// after AddLayerCompositePass, because what they modify does not exist until then.
	//
	// A Generator is upstream of both. It rewrites the layer's own input height before the layer
	// is composited at all, so everything that reads height afterwards -- the composite's height
	// blend, the mask chain's curvature, a later effect's slope -- sees the modified surface
	// rather than a carve painted over the top of a finished one. That is the whole reason the
	// category exists and the one property that must not be traded away for convenience.
	Generator UMETA(DisplayName = "Generator")
};

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatLayerChild
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid ChildId = FGuid::NewGuid();

	// Invalid means this child participates in the layer's normal ordered child chain. A valid
	// GUID scopes this child beneath another child in the same layer. Scoped masks keep their own
	// identity, bindings and instance source, but gate only their owner instead of CombinedMask.
	UPROPERTY()
	FGuid ScopeOwnerChildId;

	// Whole-child instance. With SourceChildId set, the payload, parameter bindings and source
	// assignments are drawn from the child these GUIDs name and re-read on every composite. Mask
	// instances deliberately keep their own BlendMode and Shaping.bInvert: those describe how this
	// placement joins the mask chain, not what source mask it mirrors.
	//
	// The instance keeps a ChildId of its own. It is a second place the same content appears, not
	// the same object twice: every reference, driver and instance that names this child has to go
	// on naming this one and not the source.
	UPROPERTY()
	FGuid SourceLayerId;

	UPROPERTY()
	FGuid SourceChildId;

	UPROPERTY()
	TArray<FMixtormatParameterBinding> ParameterBindings;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child", meta = (EditCondition = "Type == EMixtormatLayerChildType::RampId"))
	FMixtormatRampIdFilter RampId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child", meta = (EditCondition = "Type == EMixtormatLayerChildType::PatternId"))
	FMixtormatPatternFilter PatternId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child", meta = (EditCondition = "Type == EMixtormatLayerChildType::Blur"))
	FMixtormatMaskBlur Blur;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child", meta = (EditCondition = "Type == EMixtormatLayerChildType::Curvature"))
	FMixtormatMaskCurvature Curvature;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child", meta = (EditCondition = "Type == EMixtormatLayerChildType::CombineId"))
	FMixtormatCombineIdFilter CombineId;

	// The GENERATORS payload. One field for the whole category rather than one per generator:
	// the kind lives inside FMixtormatGenerator, so adding a second generator adds a payload
	// struct there and touches nothing here, in the child dispatch, or in any saved asset.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child", meta = (EditCondition = "Type == EMixtormatLayerChildType::Generator"))
	FMixtormatGenerator Generator;

	bool IsInstance() const { return SourceChildId.IsValid(); }
};

// A named set of adjacent layers that share one authored child stack.
//
// Groups live beside Layers rather than inside it. A header entry in the layer array would shift
// every integer a layer index means -- HeightReferenceLayerIndex above all -- so the array the
// compositor walks stays exactly the authored render order and membership is carried on the layer
// instead. Members are a contiguous run, which is what lets the editor draw one header per group
// and move a group as a single block.
//
// Children here are authored once and broadcast: MixtormatLayerGroups::BuildEffectiveLayers
// appends a remapped copy of them to every member layer immediately before composition. Nothing
// is written back, so the shared stack has exactly one authored home.
USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatLayerGroup
{
	GENERATED_BODY()

	UPROPERTY()
	FGuid GroupId = FGuid::NewGuid();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Group")
	FText DisplayName;

	// Gates the members for the render only. Member bEnabled is never rewritten, so re-enabling a
	// group returns each layer to the visibility the user gave it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Group")
	bool bEnabled = true;

	// Editor-only tint for this group's header and its members' rows. Alpha carries "set at all",
	// so a group with no colour is one field at zero rather than a second bool to keep in step.
	// Nothing in the compositor reads this -- it is organisation, not authoring.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Group", meta = (HideAlphaChannel))
	FLinearColor AccentColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

	bool HasAccentColor() const { return AccentColor.A > 0.0f; }

	// Reserved for group-level parameters. There are none yet, and these are deliberately not
	// broadcast onto member layers: a group owns no layer parameter surface to bind against, and
	// pushing them down would silently overwrite each member's own bindings.
	UPROPERTY()
	TArray<FMixtormatParameterBinding> ParameterBindings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Children")
	TArray<FMixtormatLayerChild> Children;
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

	UPROPERTY()
	FGuid LayerId = FGuid::NewGuid();

	// The group this layer belongs to, or invalid for an ungrouped layer. Membership is stored
	// here rather than as a member list on the group so that a layer can only ever be in one
	// group, and so that nothing has to be kept in step when layers move.
	UPROPERTY()
	FGuid GroupId;

	UPROPERTY()
	TArray<FMixtormatParameterBinding> ParameterBindings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	EMixtormatLayerType Type = EMixtormatLayerType::Material;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	TSoftObjectPtr<UMixtormatSurface> SourceSurface;

	// Live, isolated composition source. Material layers only; mutually exclusive with SourceSurface.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer")
	TSoftObjectPtr<UMixtormatMaterial> SourceComposition;

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
	float Tiling = 1.0f;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (ClampMin = "-1.0", ClampMax = "2.0"))
	float RoughnessContrast = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (ClampMin = "-0.5", UIMax = "0.5"))
	float RoughnessOffset = 0.0f;

	// Strength of this layer's authored normal map, as a tangent-slope gain: 0 is flat, 1 is the
	// map exactly as authored, 2 doubles its tilt. The only control that can *strengthen* an
	// imported normal -- Normal Influence runs 0..1 and can only fade one out.
	//
	// Reactivated rather than replaced. It carries the same name and type it has always been
	// serialised under, so a layer authored while it was an editable 0..2 control keeps the value
	// it was given; it was pinned neutral and hidden when Height Booster briefly owned normal
	// strength, which left no way to steepen an authored map at all.
	//
	// Deliberately not Height Booster. The booster owns the height and the normals reconstructed
	// from that height; this owns the authored map and nothing else. A surface whose height and
	// normal describe the same relief is what separates them -- tying the two made it carry the
	// same bump twice. See MixtormatReliefScaling.h.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (DisplayName = "Normal Strength", ClampMin = "0.0", UIMax = "4.0"))
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

	// How much of the height contest happens, not how strong the mask is.
	//
	// 0 is ordinary OVER -- the mask alone decides coverage and the heights simply cross-fade --
	// and 1 is the full contest gated by that mask. Past 1 the contest is already total, so the
	// rest of the range sharpens the transition instead of widening anything, which keeps the
	// slider monotone end to end.
	//
	// It used to multiply the placement mask, which made its bottom end meaningless: at 0 the
	// mask vanished from the comparison and the layer appeared wherever it happened to be taller
	// than what was beneath it, ignoring where it had been painted.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (DisplayName = "Blend Strength", EditCondition = "bHeightBlendEnabled", ClampMin = "0.0", ClampMax = "4.0"))
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

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Border normal strength derives from Border Lift."))
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
	float HeightSmoothAmount = 0.0f;

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

	// How this layer's base colour combines with what is composited below it, and how far that
	// combination is taken.
	//
	// Base colour only. A mode that suits colour rarely suits roughness -- Screen on a roughness
	// map is not a thing anyone reached for -- and roughness already carries its own Bias,
	// Contrast and Offset.
	// Gain on this layer's own height, about the 0.5 midpoint that is flat.
	//
	// A signed multiply, not a scale: height is stored centred, so this amplifies the deviation
	// from flat in both directions at once -- peaks rise and pits sink by the same factor, and
	// the surface's average level does not move. Scaling the raw 0..1 instead would lift the
	// whole layer as it exaggerated it, which reads as the layer floating rather than as relief.
	//
	// Applied to the source height before anything reads it, so displacement, the height blend
	// and the derived normals all agree. 1 is untouched, 0 is flat, and above 1 exaggerates.
	// Not the same thing as HeightInfluence, which is coverage -- how much of this layer's height
	// reaches the composite, rather than how deep that height is.
	//
	// The height and nothing else. An authored normal map is micro detail in its own right, not a
	// picture of the height, and is left at the strength it was authored with: gaining it here as
	// well made a surface whose height and normal describe the same relief carry that relief
	// twice. The normals the structural passes derive still respond, because they differentiate
	// the boosted height -- once. See MixtormatReliefScaling.h.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float HeightBoost = 1.0f;

	// Adds to this layer's boosted source height before blending, displacement, and derived normals.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition", meta = (DisplayName = "Height Offset", ClampMin = "-1.0", ClampMax = "1.0"))
	float HeightLevelOffset = 0.0f;

	// Redistributes the source height between its own ends rather than moving or scaling it: a
	// power curve, so nought and one map to themselves and only what lies between them shifts.
	// Positive bulges -- the midtones rise toward the peaks and the form reads as swollen -- and
	// negative pinches them down toward the pits. Neutral at 0.
	//
	// Named Shape rather than Bias because HeightBias is already taken, by the height-blend
	// comparison offset further up. That one moves where two layers cross; this one reshapes one
	// layer's own relief. Applied before HeightBoost, so the curve always sees a clean 0..1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition", meta = (DisplayName = "Height Shape", ClampMin = "-1.0", ClampMax = "1.0"))
	float HeightShape = 0.0f;

	// Softens this layer's own source height before anything reads it -- and only this
	// layer's, which is what separates it from the Layer Blur effect. That one is a Filter,
	// so by the time it runs there is a single target holding the whole accumulated stack and
	// it cannot tell one layer's contribution from another's. This is applied at the source,
	// before the composite merges anything, so nothing below is touched.
	//
	// In output texels, both axes together: a height smooth that was anisotropic would be a
	// different feature, and one radius is what takes the stair-stepping off a low-resolution
	// height or the hard edge off a tiling seam.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition", meta = (DisplayName = "Height Smooth", ClampMin = "0.0", ClampMax = "8.0"))
	float HeightSmooth = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition")
	EMixtormatColorBlendMode BaseColorBlendMode = EMixtormatColorBlendMode::Normal;

	// Mode strength, not opacity, and the distinction matters because there are already three
	// controls doing coverage: Opacity, the mask chain, and BaseColorInfluence. This one lerps
	// between the layer's plain colour and the blended result, so it says how much of the *mode*
	// happens -- and at Normal there is nothing to fade, so it does nothing at all.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BaseColorBlendAmount = 1.0f;

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

	// How much this layer pushes the substrate's Fuzz Slab amount. Neutral at 0, unlike the other
	// Channel Influence rows: fuzz has no underlying value every layer already carries, so a
	// layer that never touches it must leave the substrate's own default alone rather than
	// zeroing it out. Roughness remains on the master material's DA_FuzzRoughness.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel Influence", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FuzzInfluence = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel Influence")
	FLinearColor FuzzColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features")
	bool bInvertFeature = false;
};

namespace MixtormatCompositionReferences
{
	// Game-thread validation; synchronously loads transitive references, including disabled layers.
	// Pass the intended save asset's object path (also for Save As), or null for an unsaved document.
	// Checks self references, cycles, missing assets and source exclusivity without modifying assets.
	// Returns the first failure in OutError; clears it on success. Shared acyclic sources are valid.
	MIXTORMATRUNTIME_API bool Validate(
		const TArray<FMixtormatLayer>& Layers,
		const FSoftObjectPath& OwnerPath,
		FText& OutError);

	// Resolves the non-texture fuzz channel through live composition references.
	MIXTORMATRUNTIME_API float ComputeFuzzInfluence(const TArray<FMixtormatLayer>& Layers);
	MIXTORMATRUNTIME_API float ComputeFuzzInfluence(
		const TArray<FMixtormatLayer>& Layers,
		const TArray<FMixtormatLayerGroup>& Groups);

	// Strongest positive influence wins; later (topmost) layers win ties. Unset inherits the master.
	MIXTORMATRUNTIME_API TOptional<FLinearColor> ComputeFuzzColor(const TArray<FMixtormatLayer>& Layers);
	MIXTORMATRUNTIME_API TOptional<FLinearColor> ComputeFuzzColor(
		const TArray<FMixtormatLayer>& Layers,
		const TArray<FMixtormatLayerGroup>& Groups);
}

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

	// Groups over the layers above. Order here is storage order only -- a group's position in the
	// stack is the position of its first member, so this array never has to be kept sorted.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layers", meta = (TitleProperty = "DisplayName"))
	TArray<FMixtormatLayerGroup> LayerGroups;

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

	// The same bake, wrapped so a layer can source it. A layer points at a UMixtormatSurface
	// rather than at four loose textures, and it reads height from the RAM alpha -- which the
	// bake fills with F0 -- so this carries its own repacked RAMH rather than reusing BakedRAM.
	//
	// Owned by the recipe, so its lifetime is the recipe's: there is nothing provisional to
	// promote on save or collect on discard. Per-pixel F0 does not survive the repack; the
	// surface carries a single DefaultIOR in its place.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bake")
	TSoftObjectPtr<UMixtormatSurface> BakedSurface;

	// A document-level quarter turn applied after composition. The GPU output pass also rotates
	// tangent-space normal XY, so the baked normal remains aligned with the rotated channels.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Canvas")
	bool bRotateUV90 = false;
};
