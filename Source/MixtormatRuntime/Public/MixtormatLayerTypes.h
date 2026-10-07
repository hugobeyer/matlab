// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"
#include "MixtormatHeightTypes.h"
#include "MixtormatMaskTypes.h"
#include "MixtormatIdTypes.h"
#include "MixtormatGeneratorTypes.h"
#include "MixtormatEffect.h"
#include "MixtormatMaskBlur.h"
#include "MixtormatMaskCurvature.h"
#include "MixtormatOutputReference.h"
#include "MixtormatParameterTypes.h"
#include "MixtormatLayerTypes.generated.h"

class UMixtormatMaterial;
class UMixtormatSurface;
class UTexture2D;

UENUM(BlueprintType)
enum class EMixtormatLayerType : uint8
{
	Material UMETA(DisplayName = "Material Layer"),
	Fill UMETA(DisplayName = "Fill Layer"),
	Generator UMETA(DisplayName = "Generator Layer")
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
	Generator UMETA(DisplayName = "Generator"),
	// The two halves Pattern IDs used to carry itself, as ordinary Region-ID consumers. Appended,
	// like everything below ColorId, and they read the nearest valid map above them rather than
	// asking which node produced it -- Pattern, Cluster and Combine are all equally valid sources.
	UvFromIds UMETA(DisplayName = "UV From IDs"),
	ReliefFromIds UMETA(DisplayName = "Relief From IDs"),
	// Appended for serialization safety. Owns ordered scoped Region-ID references/legacy producers.
	IdGroup UMETA(DisplayName = "ID Group"),
	OutputReference UMETA(DisplayName = "Output Reference"),
	BoundaryFromIds UMETA(DisplayName = "Boundary From IDs"),
	// Generator-layer sublayers. Ordered in the layer's child chain alongside Generator modules;
	// they read and rewrite the running signed generator height. Appended for serialization safety.
	HeightBlend UMETA(DisplayName = "Height Blend"),
	HeightCurve UMETA(DisplayName = "Height Remap"),
	HeightColorRamp UMETA(DisplayName = "Color Ramp")
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child", meta = (EditCondition = "Type == EMixtormatLayerChildType::UvFromIds"))
	FMixtormatUvIdFilter UvId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child", meta = (EditCondition = "Type == EMixtormatLayerChildType::ReliefFromIds"))
	FMixtormatReliefIdFilter ReliefId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child", meta = (EditCondition = "Type == EMixtormatLayerChildType::IdGroup"))
	FMixtormatIdGroup IdGroup;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child", meta = (EditCondition = "Type == EMixtormatLayerChildType::OutputReference"))
	FMixtormatOutputReference OutputReference;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child", meta = (EditCondition = "Type == EMixtormatLayerChildType::BoundaryFromIds"))
	FMixtormatBoundaryIdFilter BoundaryId;

	// Generator-layer sublayer payloads. Ordered in the layer's child chain with the Generator
	// modules; each rewrites the running signed generator height (or publishes colour) in place.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child", meta = (EditCondition = "Type == EMixtormatLayerChildType::HeightBlend"))
	FMixtormatGeneratorHeightBlend HeightBlend;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child", meta = (EditCondition = "Type == EMixtormatLayerChildType::HeightCurve"))
	FMixtormatGeneratorHeightCurve HeightCurve;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Child", meta = (EditCondition = "Type == EMixtormatLayerChildType::HeightColorRamp"))
	FMixtormatGeneratorHeightColorRamp HeightColorRamp;

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

	// Generator-layer output routing. Color producers remain independent of this destination choice.
	// On by default: a generator with a Color Ramp is expected to paint it, and the compositor
	// skips the write entirely when no colour is generated, so the default is safe.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generator Output", meta = (DisplayName = "Albedo"))
	bool bGeneratorAlbedo = true;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Opacity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	bool bOverrideBaseColor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (EditCondition = "bOverrideBaseColor"))
	FLinearColor BaseColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	bool bOverrideRoughness = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (EditCondition = "bOverrideRoughness", UIMin = "0.0", UIMax = "1.0"))
	float Roughness = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	bool bOverrideIOR = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (EditCondition = "bOverrideIOR", UIMin = "1.0", UIMax = "3.0"))
	float IOR = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
	bool bOverrideMetallic = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (EditCondition = "bOverrideMetallic", UIMin = "0.0", UIMax = "1.0"))
	float Metallic = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (UIMin = "1.0", UIMax = "8.0", Delta = "1.0"))
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (UIMin = "1", UIMax = "16"))
	int32 UVScaleX = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (UIMin = "1", UIMax = "16"))
	int32 UVScaleY = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (UIMin = "-1.0", UIMax = "1.0"))
	float UVOffsetX = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (UIMin = "-1.0", UIMax = "1.0"))
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (UIMin = "0.0", UIMax = "1.0"))
	float RoughnessBias = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (UIMin = "-1.0", UIMax = "2.0"))
	float RoughnessContrast = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (UIMin = "-0.5", UIMax = "0.5"))
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments", meta = (DisplayName = "Normal Strength", UIMin = "0.0", UIMax = "4.0"))
	float NormalIntensity = 1.0f;

	// Degrees. The composite pass divides by 360 and wraps, so the clamp is a half turn either
	// way -- past that a shift is indistinguishable from the shorter rotation the other side.
	// Editor rows are normalised -1..1 and scale by MixtormatHue::DegreesPerUnit to get here.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Adjustments", meta = (UIMin = "-180.0", UIMax = "180.0"))
	float HueShift = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Adjustments", meta = (UIMin = "0.0", UIMax = "2.0"))
	float Saturation = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Color Adjustments", meta = (UIMin = "0.0", UIMax = "2.0"))
	float Value = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Adjustments")
	EMixtormatNormalBlendMode NormalBlendMode = EMixtormatNormalBlendMode::Combine;

	// How this layer's height combines with the height below it, weighted by its coverage. At Op =
	// Height Blend the heights decide that coverage too; every other op takes it from the mask.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition")
	FMixtormatHeightBlend HeightBlend;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending")
	EMixtormatHeightSource HeightSource = EMixtormatHeightSource::LayerHeight;

	// The layer's Height Blend Strength: how much of the height contest happens, not how strong
	// the mask is. It lives here rather than in HeightBlend because a Driver binds a flat property
	// of the layer, and this is the driven one (slot 1).
	//
	// 0 is ordinary OVER -- the mask alone decides coverage and the heights simply cross-fade --
	// and 1 is the full contest gated by that mask. Past 1 the contest is already total, so the
	// rest of the range sharpens the transition instead of widening anything, which keeps the
	// slider monotone end to end.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (DisplayName = "Blend Strength", UIMin = "0.0", UIMax = "4.0"))
	float HeightBlendAmount = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (UIMin = "0.01"))
	float HeightContrast = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending")
	bool bInvertHeight = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (UIMin = "0.0", UIMax = "1.0"))
	float ConstantHeight = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (UIMin = "0.0", UIMax = "1.0"))
	float MaskHeightInfluence = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (UIMin = "0.0", UIMax = "1.0"))
	float HeightContactAOAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (UIMin = "0.0001", UIMax = "1.0"))
	float HeightContactAOWidth = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (UIMin = "-1.0", UIMax = "1.0"))
	float HeightBorderLift = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (UIMin = "0.0001", UIMax = "1.0"))
	float HeightBorderWidth = 0.05f;

	// Gaussian radius, in texels, applied to the accumulated height that Contact AO and Border
	// Normal are built from. 1 skips the two blur passes entirely.
	//
	// Width is a softness in the height domain: it widens the band without changing how the field
	// is sampled, so raising it gave a wider band that was just as noisy. This smooths the height
	// before the field is derived from it, and only those two effects read the smoothed copy --
	// coverage, layer height and blend weight all keep the sharp one.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (UIMin = "1.0", UIMax = "32.0"))
	float HeightBorderSmoothing = 1.0f;

	// Rounds the height the placement mask produces, in texels. 0 skips the two blur passes
	// entirely. Wide enough and the interior of a shape domes rather than only its rim softening,
	// which is the difference between an anti-aliased edge and a filleted one.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (UIMin = "0.0", UIMax = "32.0"))
	float HeightSmoothRadius = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending", meta = (UIMin = "0.0", UIMax = "1.0"))
	float HeightSmoothAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Blending")
	int32 HeightReferenceLayerIndex = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Children", meta = (TitleProperty = "Type"))
	TArray<FMixtormatLayerChild> Children;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features", meta = (UIMin = "0.0", UIMax = "1.0"))
	float FeatureInfluence = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features", meta = (UIMin = "0.0", UIMax = "1.0"))
	float FeatureBias = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features", meta = (UIMin = "0.0", UIMax = "1.0"))
	float HeightFeatureInfluence = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features")
	bool bInvertHeightFeature = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features", meta = (UIMin = "0.0", UIMax = "1.0"))
	float AOFeatureInfluence = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features")
	bool bInvertAOFeature = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features", meta = (UIMin = "1", UIMax = "32"))
	int32 CurvatureRadius = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features", meta = (UIMin = "0.0"))
	float CurvatureStrength = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features", meta = (UIMin = "0.001"))
	float CurvaturePower = 1.0f;

	// Averages curvature over this many widening rings. 1 is a single kernel.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features", meta = (UIMin = "1", UIMax = "4"))
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition", meta = (UIMin = "0.0", UIMax = "4.0"))
	float HeightBoost = 1.0f;

	// Adds to this layer's boosted source height before blending, displacement, and derived normals.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition", meta = (DisplayName = "Height Offset", UIMin = "-1.0", UIMax = "1.0"))
	float HeightLevelOffset = 0.0f;

	// Redistributes the source height between its own ends rather than moving or scaling it: a
	// power curve, so nought and one map to themselves and only what lies between them shifts.
	// Positive bulges -- the midtones rise toward the peaks and the form reads as swollen -- and
	// negative pinches them down toward the pits. Neutral at 0.
	//
	// Named Shape rather than Bias because Bias already means the height-blend comparison offset
	// (HeightBlend.BaseBias). That one moves where two layers cross; this one reshapes one
	// layer's own relief. Applied before HeightBoost, so the curve always sees a clean 0..1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition", meta = (DisplayName = "Height Shape", UIMin = "-1.0", UIMax = "1.0"))
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition", meta = (DisplayName = "Height Smooth", UIMin = "0.0", UIMax = "8.0"))
	float HeightSmooth = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition")
	EMixtormatColorBlendMode BaseColorBlendMode = EMixtormatColorBlendMode::Normal;

	// Mode strength, not opacity, and the distinction matters because there are already three
	// controls doing coverage: Opacity, the mask chain, and BaseColorInfluence. This one lerps
	// between the layer's plain colour and the blended result, so it says how much of the *mode*
	// happens -- and at Normal there is nothing to fade, so it does nothing at all.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composition", meta = (UIMin = "0.0", UIMax = "1.0"))
	float BaseColorBlendAmount = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel Influence", meta = (UIMin = "0.0", UIMax = "1.0"))
	float BaseColorInfluence = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel Influence", meta = (UIMin = "0.0", UIMax = "1.0"))
	float RoughnessInfluence = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel Influence", meta = (UIMin = "0.0", UIMax = "1.0"))
	float AOInfluence = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel Influence", meta = (UIMin = "0.0", UIMax = "1.0"))
	float MetallicInfluence = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel Influence", meta = (DisplayName = "IOR / F0 Influence", UIMin = "0.0", UIMax = "1.0"))
	float F0Influence = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel Influence", meta = (UIMin = "0.0", UIMax = "1.0"))
	float NormalInfluence = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel Influence", meta = (UIMin = "0.0", UIMax = "1.0"))
	float HeightInfluence = 1.0f;

	// How much this layer pushes the substrate's Fuzz Slab amount. Neutral at 0, unlike the other
	// Channel Influence rows: fuzz has no underlying value every layer already carries, so a
	// layer that never touches it must leave the substrate's own default alone rather than
	// zeroing it out. Roughness remains on the master material's DA_FuzzRoughness.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel Influence", meta = (UIMin = "0.0", UIMax = "1.0"))
	float FuzzInfluence = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Channel Influence")
	FLinearColor FuzzColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Generated Features")
	bool bInvertFeature = false;
};
