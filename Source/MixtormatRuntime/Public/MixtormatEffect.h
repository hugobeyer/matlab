// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UObject/SoftObjectPtr.h"
#include "MixtormatMaskShaping.h"
#include "MixtormatEffect.generated.h"

class UMixtormatMask;
class UTexture2D;

// Surface effects write coverage, normal and AO through the effect data target.
// Filter effects write no effect data. Erosion and Breakup carve height and Grade transforms
// colour, all three after the layer composites and all three the identity at zero amount.
// Stain is the exception inside this class: it writes no effect data either, but it resolves a
// layer mask inside the child loop rather than transforming composited channels afterwards.
// The class exists to answer one question -- does this effect write the effect data target --
// and the answer for Stain is still no.
UENUM(BlueprintType)
enum class EMixtormatEffectClass : uint8
{
	Surface = 0 UMETA(DisplayName = "Surface"),
	Filter = 1 UMETA(DisplayName = "Filter")
};

UENUM(BlueprintType)
enum class EMixtormatEffectType : uint8
{
	Peeling = 0 UMETA(DisplayName = "Peeling"),
	Stain = 1 UMETA(DisplayName = "Stain"),
	Erosion = 2 UMETA(DisplayName = "Erosion"),
	Grade = 3 UMETA(DisplayName = "Grade"),
	// Slot 4 was Chipping. Breakup replaces its implementation in place rather than being
	// appended: serialized recipes store this enum by value, so keeping the number is what makes
	// an existing asset load as the new effect. Config/DefaultMixtormat.ini carries the name
	// redirect for anything that serialized it by name.
	Breakup = 4 UMETA(DisplayName = "Breakup"),
	WornEdges = 5 UMETA(DisplayName = "Worn Edges"),
	// Appended: serialized recipes store this enum by value.
	FlowWarp = 6 UMETA(DisplayName = "Flow Warp"),
	// The mask blur turned outward: that one softens a mask, this softens the surface the
	// stack has built. A Filter, because it wants the composited result rather than the
	// effect data target -- and being an Effect is what lets a mask be scoped under it.
	LayerBlur = 7 UMETA(DisplayName = "Layer Blur"),
	// The cheap procedural answer to Wet Stain for vertical runoff: streaks, mineral buildup and
	// dirt under ledges. Where Stain transports liquid over tens of ping-ponged iterations,
	// Runoff smears one prepared source field along gravity with a directional Gaussian and
	// stacks a few strata of it. Not a replacement -- Stain still solves things a smear cannot,
	// like liquid pooling and flowing around an obstacle -- but for a streak it is two passes
	// instead of twenty-two. A mask child, same as Stain: it resolves the shape of where the
	// runoff ran into the layer's mask chain and the layer supplies every channel.
	Runoff = 8 UMETA(DisplayName = "Runoff")
};

UENUM(BlueprintType)
enum class EMixtormatLayerBlurScope : uint8
{
	// Gated by layer coverage multiplied by the independently evaluated scoped mask chain.
	//
	// It does not isolate this layer's contribution, and the name used to imply that it did. By
	// the time a Filter runs there is one surface target holding the whole accumulated stack, so
	// what gets blurred inside the coverage is that composite -- including whatever of the layers
	// beneath shows through wherever this layer is not fully opaque. A layer with no mask at all
	// has full coverage, which is white, so this blurs everything.
	//
	// Isolating the layer would mean blurring before the composite merges it, which is a
	// different insertion point than a Filter has.
	Layer = 0 UMETA(DisplayName = "Layer Coverage"),
	// Ungated by layer coverage: softens the whole accumulated surface. A scoped mask child still
	// gates it, which is how this becomes a lens blur over a chosen region.
	Composite = 1 UMETA(DisplayName = "Whole Composite")
};

// How a Breakup family combines with the one coarser than it. The three are the standard CSG
// set on a signed distance field, so they compose the way an artist expects: union adds fragments,
// subtract cuts the finer family out of the coarser, intersect keeps only the overlap.
UENUM(BlueprintType)
enum class EMixtormatBreakupOperation : uint8
{
	Union = 0 UMETA(DisplayName = "Union"),
	Subtract = 1 UMETA(DisplayName = "Subtract"),
	Intersect = 2 UMETA(DisplayName = "Intersect")
};

UENUM(BlueprintType)
enum class EMixtormatFlowWarpBlendMode : uint8
{
	Replace = 0 UMETA(DisplayName = "Replace"),
	MinHeight = 1 UMETA(DisplayName = "Min Height"),
	MaxHeight = 2 UMETA(DisplayName = "Max Height")
};

// The one place the Surface/Filter split is decided. It used to be declared and never called,
// so the taxonomy was a comment while every dispatch site tested effect types by hand -- and
// each new Filter meant finding all of them again. A Filter is deferred out of the child loop
// and run over the layer's composited output; a Surface writes the effect data target.
inline EMixtormatEffectClass MixtormatEffectClassOf(const EMixtormatEffectType Type)
{
	switch (Type)
	{
	case EMixtormatEffectType::Stain:
	case EMixtormatEffectType::Erosion:
	case EMixtormatEffectType::Grade:
	case EMixtormatEffectType::Breakup:
	case EMixtormatEffectType::WornEdges:
	case EMixtormatEffectType::FlowWarp:
	case EMixtormatEffectType::LayerBlur:
	case EMixtormatEffectType::Runoff:
		return EMixtormatEffectClass::Filter;
	default:
		return EMixtormatEffectClass::Surface;
	}
}

UCLASS(BlueprintType)
class MIXTORMATRUNTIME_API UMixtormatEffect final : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "Identity")
	FName Category = TEXT("Peeling");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "Identity")
	EMixtormatEffectType EffectType = EMixtormatEffectType::Peeling;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	FString SourceTextureBaseName;

	// The authored map set -- peel data, coverage mask, height, SDF, bent normal and the decode
	// ranges that went with them -- lived here. Peeling is generated now, so an effect asset
	// names a type and carries defaults; it no longer ships textures.

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults", meta = (UIMin = "0.0", UIMax = "1.0"))
	float DefaultFront = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults", meta = (UIMin = "0.000001"))
	float DefaultWidth = 0.015f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults")
	float DefaultMacroWarp = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults")
	float DefaultMicroWarp = 0.003f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults", meta = (UIMin = "0.0", UIMax = "1.0"))
	float DefaultMicroMorph = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults", meta = (UIMin = "0.0"))
	float DefaultThickness = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults", meta = (UIMin = "0.0"))
	float DefaultLift = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults", meta = (UIMin = "0.0"))
	float DefaultDetailStrength = 0.02f;

	// Gather-era stain defaults. Nothing reads any of them: a Stain child runs the transport
	// solve on its own defaults, and the solve shades nothing, so an authored colour and
	// roughness have nowhere to go.
	//
	// Deprecated rather than deleted, and deliberately no longer EditAnywhere. Left editable they
	// were worse than dead code -- a "Stain Defaults" category on MLFX_Stain that a person could
	// open, tune, and save, with no effect anywhere. The fields stay so the existing asset loads
	// without dropping them.
	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Stain resolves a layer mask and shades nothing."))
	FLinearColor DefaultStainColor = FLinearColor(0.22f, 0.09f, 0.035f, 1.0f);

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Stain resolves a layer mask and shades nothing."))
	float DefaultStainRoughness = 0.2f;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Replaced by the stain transport solve."))
	float DefaultStainHeightInfluence = 0.5f;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Replaced by the stain transport solve."))
	float DefaultStainHeightWarp = 0.35f;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Replaced by the stain auto-source weights."))
	float DefaultStainHeightBias = -1.0f;

	UPROPERTY(meta = (DeprecatedProperty, DeprecationMessage = "Replaced by the stain auto-source weights."))
	float DefaultStainHeightContrast = 1.0f;
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

// Both stain looks use the same transport solve. Wet exposes absorbed liquid; Deposit exposes
// the dried dirt/mineral residue left behind by that liquid.
UENUM(BlueprintType)
enum class EMixtormatStainMode : uint8
{
	Wet = 0 UMETA(DisplayName = "Wet"),
	Deposit = 1 UMETA(DisplayName = "Deposit")
};

	// Peel edge profile. Curled lifts a flap ahead of the front and folds it back behind.
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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effect", meta = (UIMin = "0.0", UIMax = "1.0"))
	float Strength = 1.0f;

	// Negative erodes inside the mask contour, positive dilates outside it. The procedural
	// field is a signed distance, so both directions are meaningful; the lower bound used to
	// be 0 because the field could only ever dilate. Widening a clamp changes no serialized
	// value, so the authored path still resolves identically for any input it already held.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling", meta = (UIMin = "-1.0", UIMax = "1.0"))
	float Front = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling", meta = (UIMin = "0.000001"))
	float Width = 0.015f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling")
	float MacroWarp = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling")
	float MicroWarp = 0.003f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling", meta = (UIMin = "0.0", UIMax = "1.0"))
	float MicroMorph = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling", meta = (UIMin = "0.0"))
	float Thickness = 0.04f;

	// How far the sheet rises where it is still attached beside a tear.
	//
	// Retuned from 0.04 together with the curl length it is spread over. The old pair described a
	// rise of 0.04 across roughly 430 texels, a slope of 0.0001, which is geometrically flat --
	// it only ever read because the peel's normal pass exaggerated its gradient about 256x. With
	// that pass on the shared height->normal convention the lift has to be real relief, so it is
	// now a few millimetres over a few centimetres of sheet rather than a hair over a fifth of
	// the tile.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling", meta = (UIMin = "0.0"))
	float Lift = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling", meta = (UIMin = "0.0"))
	float DetailStrength = 0.02f;

	// Procedural peeling generates its field from noise and the surface composited below.
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
	// Optional seed mask. Independent of scoped mask children; unset means no external mask
	// contributes to seeding.
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural", meta = (UIMin = "0.0", UIMax = "1.0"))
	float PeelCornerLift = 0.6f;

	// Size of the window the corner detector looks through, as a multiple of the curl length.
	//
	// A wider window responds to broader features and spreads the boost further back from the
	// tip, so raising this lifts bigger pieces of sheet rather than only their sharpest points.
	// Narrow it to pick out fine serrations along a tear.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural", meta = (DisplayName = "Corner Radius", UIMin = "0.05", UIMax = "4.0"))
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Peeling|Procedural", meta = (DisplayName = "ID Influence", UIMin = "0.0", UIMax = "1.0"))
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

	// Curvature below the layer and any same-layer generator normal participate in source
	// placement. Positive values add liquid/dirt at concave or convex detail.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Surface")
	float StainConcavityWeight = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Surface")
	float StainConvexityWeight = 0.45f;

	// Occlusion, height and slope complete the auto source from the accumulated surface below.
	// Curvature can also use a same-layer generator normal. Zero by default: curvature alone is
	// the conservative starting point.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Surface")
	float StainOcclusionWeight = 0.0f;

	// Signed. Positive sources runoff from high ground, negative pools liquid in the low.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stain|Surface")
	float StainHeightWeight = 0.0f;

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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runoff", meta = (UIMin = "-180.0", UIMax = "180.0", Delta = "1.0"))
	float RunoffGravityAngle = -90.0f;

	// How far runoff reaches from its source, in texels at 1K. Read as a fraction of the longer
	// side rather than as literal texels, so the same number is the same run at 1K, 2K and 4K --
	// a streak that shortened when the composition grew would make the control meaningless.
	//
	// This is the reach of the strongest source. A weaker mask value reaches proportionally less,
	// which is what makes the incoming mask a length control and not only an opacity.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runoff", meta = (UIMin = "8.0", UIMax = "512.0", Delta = "1.0"))
	float RunoffStreakRadius = 320.0f;

	// The Gaussian's sigma as a fraction of reach. Low values keep a run tight and defined and
	// stop it abruptly; high values let it fade out over most of its length. It also widens the
	// terminal lip, because a soft run deposits over a longer stretch than a sharp one does.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runoff", meta = (UIMin = "0.05", UIMax = "1.0", Delta = "0.01"))
	float RunoffStreakSoftness = 0.46f;

	// How much the height underneath decides where runoff starts. At 1 only cavities and the
	// upper edges of ledges source it, which is where dirt actually collects. At 0 the height is
	// ignored and the incoming mask alone is the source, which is how to streak from a painted
	// mark rather than from the surface's own shape.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runoff", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float RunoffSurfaceInfluence = 0.95f;

	// How strongly the stacked layers read as separate deposits. Not a count: it widens their
	// spacing, spreads their lengths and flattens their opacity falloff all at once, so 0 is a
	// single coherent run and 1 is a visibly layered, uneven buildup. The count itself follows
	// from Streak Radius -- a short run has no room to show five strata.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runoff", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float RunoffStrataAmount = 0.75f;

	// Feature size of the internal fractal noise, as cells across the texture. It does double
	// duty deliberately: the same field breaks up the source before the smear and warps each
	// stratum's length, so one control changes the grain of the whole effect rather than needing
	// a separate noise scale nobody would match to it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runoff", meta = (UIMin = "1.0", UIMax = "32.0", Delta = "1.0"))
	float RunoffWarpScale = 18.0f;

	// How far that noise pushes each stratum's endpoint along gravity. Only along gravity -- a
	// sideways push would turn a run into a smudge. Above 1 the strata pull apart far enough to
	// read as independent runs from the same source.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runoff", meta = (UIMin = "0.0", UIMax = "2.0", Delta = "0.01"))
	float RunoffWarpAmount = 1.5f;

	// The narrow deposit left where a run stops, the way a drying streak leaves a tidemark. 0
	// ends every run on a clean fade; 1 puts a defined crust at the end of each stratum.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runoff", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float RunoffLipStrength = 0.55f;

	// Overall weight of the resolved runoff in the layer's mask chain. 0 is the identity.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runoff", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float RunoffStrength = 0.25f;

	// Every random choice the effect makes -- the noise field, the per-stratum decorrelation --
	// comes off this. Same seed, same runoff, at any resolution.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runoff", meta = (UIMin = "0", UIMax = "9999", Delta = "1"))
	int32 RunoffSeed = 1;

	// Erosion. A post-layer directional horizon filter. Exposed points seed a bounded
	// eikonal envelope measured against the immutable input height, so wear propagates at a
	// controllable Unit Distance without repeatedly destroying the source silhouette.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion", meta = (UIMin = "0.0", UIMax = "8.0", Delta = "0.01"))
	float ErosionAmount = 1.5f;

	// Maximum local seed depth multiplier. The shader scales this into normalized height units;
	// the eikonal envelope only propagates the resulting source-relative carve offset.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion", meta = (UIMin = "0.0", UIMax = "2.0", Delta = "0.01"))
	float ErosionDepth = 1.0f;

	// UV distance per unit of vertical change for the eikonal wear envelope.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion", meta = (UIMin = "0.01", UIMax = "1.0", Delta = "0.01"))
	float ErosionUnitDistance = 0.3f;

	// Direction toward the horizon light: X/Y lie in the texture tangent plane; Z is elevation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion", meta = (UIMin = "-1.0", UIMax = "1.0", Delta = "0.01"))
	float ErosionDirectionX = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion", meta = (UIMin = "-1.0", UIMax = "1.0", Delta = "0.01"))
	float ErosionDirectionY = -1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float ErosionDirectionZ = 0.2f;

	// Ray sample span in texels; iterations propagate the envelope over this stepped stencil.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion", meta = (UIMin = "1", UIMax = "3", Delta = "1"))
	int32 ErosionRadius = 1;

	// Ping-pong eikonal relaxations. More passes propagate the bounded wear envelope farther;
	// the original composited height remains the obstacle and is never used as a moving target.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion", meta = (UIMin = "1", UIMax = "128", Delta = "1"))
	int32 ErosionIterations = 8;

	// Legacy direction bias retained for serialized effects; the inspector now authors the
	// explicit X/Y/Z horizon direction above.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float ErosionGravityForce = 0.6f;

	// Power applied to horizon exposure before it seeds the carve envelope.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion", meta = (UIMin = "0.1", UIMax = "1.0", Delta = "0.01"))
	float ErosionSlopePower = 1.0f;

	// Refill-only downstream smear, capped by the original source height. 0 disables deposition.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion", meta = (UIMin = "0.0", UIMax = "4.0", Delta = "0.01"))
	float ErosionDeposit = 0.25f;

	// Minimum normalized horizon exposure required to seed wear.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion", meta = (UIMin = "0.0", UIMax = "0.5", Delta = "0.001"))
	float ErosionPreserveFlats = 0.12f;

	// Legacy smoothing value retained for serialized effects; the inspector presents this as
	// horizon feathering in the eikonal version.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float ErosionSmoothing = 0.65f;

	// Secondary seed density from angular flow noise around horizon boundaries.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float ErosionSecondaryAmount = 0.5f;

	// Seeded tileable noise and Voronoi hardness variation on the eikonal seed depth.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float ErosionVariation = 0.18f;

	// Seeds the variation field. Same seed, same variation, at any resolution.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion", meta = (UIMin = "0", UIMax = "9999", Delta = "1"))
	int32 ErosionSeed = 1;

	// Optional placement mask owned by Erosion. When unset, the filter keeps using the layer's
	// accumulated authored, generated, and craquelure mask children.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion|Placement")
	TSoftObjectPtr<UMixtormatMask> ErosionMask;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion|Placement")
	TSoftObjectPtr<UTexture2D> ErosionMaskTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion|Placement", meta = (UIMin = "1", UIMax = "16", Delta = "1"))
	int32 ErosionMaskTiling = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion|Placement")
	bool bErosionInvertMask = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion", meta = (UIMin = "-1.0", UIMax = "1.0", Delta = "0.01"))
	float ErosionRoughnessAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Erosion", meta = (UIMin = "0.001", UIMax = "1.0", Delta = "0.001"))
	float ErosionCarveDepth = 0.05f;

	// Grade. Transforms the base colour composited up to this layer, masked by the layer's
	// own mask children, which makes it an adjustment layer rather than a per-texture tweak.
	//
	// Applied in this order, and the order is the point: brightness and contrast are linear
	// operations and belong above the tonemap, gamma is display shaping and belongs below it.
	//
	//     Brightness -> Contrast (about Pivot) -> Tonemap -> Gamma
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float GradeAmount = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade")
	EMixtormatGradeTonemap GradeTonemap = EMixtormatGradeTonemap::None;

	// Blend between the untonemapped and tonemapped result, so an operator can be dialled in
	// rather than only switched on.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float GradeTonemapStrength = 1.0f;

	// A gain, not an offset: scaling linear values behaves like exposure and leaves hue
	// alone, where adding a constant washes saturation out of the darks.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade", meta = (UIMin = "0.0", UIMax = "4.0", Delta = "0.01"))
	float GradeBrightness = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade", meta = (UIMin = "0.0", UIMax = "4.0", Delta = "0.01"))
	float GradeContrast = 1.0f;

	// The value contrast pivots about. 0.18 is linear mid grey and is correct for this data;
	// 0.5 is what display-referred habits reach for, so it is a control rather than a
	// constant.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float GradeContrastPivot = 0.18f;

	// Applied as pow(c, 1 / Gamma), so above 1 lifts the midtones. That is the convention
	// every grading UI uses and the reciprocal is easy to get backwards.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade", meta = (UIMin = "0.05", UIMax = "4.0", Delta = "0.01"))
	float GradeGamma = 1.0f;

	// Levels remap, applied first and ahead of Brightness/Contrast: t = (value - Min) / (Max -
	// Min), clamped to 0..1, then value = lerp(OutputMin, OutputMax, t). At the identity range
	// (0..1 in, 0..1 out) this is a no-op, which is what keeps every grade already authored
	// against Brightness/Contrast/Gamma unchanged.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade|Levels", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float GradeInputMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade|Levels", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float GradeInputMax = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade|Levels", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float GradeOutputMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade|Levels", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float GradeOutputMax = 1.0f;

	// Per-channel offset, added after the levels remap and the linear Brightness/Contrast stage
	// but ahead of the tonemap, so a colour cast can be dialled in on data the tonemap has not
	// yet reshaped. Zero on every channel is the identity.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade|Levels", meta = (UIMin = "-1.0", UIMax = "1.0", Delta = "0.01"))
	float GradeBiasR = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade|Levels", meta = (UIMin = "-1.0", UIMax = "1.0", Delta = "0.01"))
	float GradeBiasG = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grade|Levels", meta = (UIMin = "-1.0", UIMax = "1.0", Delta = "0.01"))
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (UIMin = "1", UIMax = "64", Delta = "1"))
	int32 BreakupScale = 6;

	// How far apart the derived Mid and Detail families sit from Macro. Low keeps all three near
	// the same size and the result reads as one population; high spreads them and gives large
	// plates broken by much finer fragments.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float BreakupDetail = 0.5f;

	// Fraction of cells that are present at all. Below 1 the field has genuine gaps, which is what
	// separates scattered flakes from continuous plating.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float BreakupDensity = 0.72f;

	// Piece size as a fraction of its own cell, so it tracks Scale instead of fighting it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (UIMin = "0.001", UIMax = "1.0", Delta = "0.005"))
	float BreakupSize = 0.32f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Advanced", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float BreakupSizeVariation = 0.3125f;

	// Maximum aspect ratio a piece may be drawn at. Applied in both directions, so a single
	// control gives both elongated and squat fragments rather than a directional bias.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (UIMin = "0.05", UIMax = "4.0", Delta = "0.01"))
	float BreakupStretch = 1.6f;

	// Blends each piece from a box metric toward a diamond one: rounded flakes at 0, angular
	// shards at 1. A shape control rather than a second noise, so it changes what a fragment is
	// instead of where it sits.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float BreakupAngularity = 0.72f;

	// How far a piece may wander off its lattice cell.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float BreakupIrregularity = 0.38f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Advanced")
	EMixtormatBreakupOperation BreakupMidOperation = EMixtormatBreakupOperation::Union;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Advanced")
	EMixtormatBreakupOperation BreakupDetailOperation = EMixtormatBreakupOperation::Union;

	// Blend radius of the CSG operations, as a fraction of a piece. 0 is a hard boolean.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Advanced", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float BreakupSmoothness = 0.30f;

	// Signed offset of the final field in reference pixels. Positive shrinks pieces and opens
	// space between them; negative grows them before relief is applied.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (UIMin = "-64.0", UIMax = "64.0", Delta = "0.25"))
	float BreakupInset = 0.0f;

	// Warps the whole field before the cells are evaluated, in reference pixels at 1K. Built from
	// integer-period sinusoids so the result still closes on the UV square exactly.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (UIMin = "0.0", UIMax = "32.0", Delta = "0.1"))
	float BreakupDistortion = 5.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Advanced", meta = (UIMin = "1", UIMax = "16", Delta = "1"))
	int32 BreakupDistortionFrequency = 3;

	// Signed, and the main structural control. Negative carves the pieces into the surface --
	// torn, flaked, recessed. Zero leaves the surface alone and lets Fold and Crease do the work.
	// Positive stands them proud, for rock foundations and raised plates.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (UIMin = "-0.5", UIMax = "0.5", Delta = "0.0025"))
	float BreakupRelief = -0.06f;

	// Dedicated per-piece relief scaling. Kept separate from Variation so plate/flake thickness can
	// change without making crease, fold and push equally noisy.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float BreakupThicknessVariation = 0.30f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (UIMin = "0.0", UIMax = "64.0", Delta = "0.1"))
	float BreakupGapWidth = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (UIMin = "0.0", UIMax = "0.5", Delta = "0.001"))
	float BreakupGapDepth = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float BreakupGapVariation = 0.35f;

	// Raises the material just outside each piece. The lip of torn paper, peeling paint, curled
	// mud or a lifting ice plate.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (UIMin = "0.0", UIMax = "0.5", Delta = "0.0025"))
	float BreakupFold = 0.025f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Advanced", meta = (UIMin = "0.001", UIMax = "64.0", Delta = "0.25"))
	float BreakupFoldWidth = 16.0f;

	// Cuts a narrow depression along the zero crossing itself: cracks, plate separation, torn
	// seams, rock joints. Works with Relief at 0, which is the crack-only configuration.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (UIMin = "0.0", UIMax = "0.5", Delta = "0.001"))
	float BreakupCrease = 0.018f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Advanced", meta = (UIMin = "0.001", UIMax = "32.0", Delta = "0.05"))
	float BreakupCreaseWidth = 1.25f;

	// Warps the incoming height along the field gradient instead of replacing it: compressed
	// material, pushed rock, bulging, warped strata. In reference pixels at 1K, so the visual
	// scale holds between a preview and a 4K bake.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (UIMin = "-128.0", UIMax = "128.0", Delta = "0.25"))
	float BreakupPush = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Advanced", meta = (UIMin = "0.001", UIMax = "64.0", Delta = "0.5"))
	float BreakupPushWidth = 24.0f;

	// Height separation added by Push. This makes Push visible even when the incoming height is flat;
	// the existing Push value still controls the signed UV advection distance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (UIMin = "0.0", UIMax = "0.5", Delta = "0.001"))
	float BreakupPushRelief = 0.035f;

	// Per-piece variation of relief, fold, crease and push, off the field's own stable piece id.
	// It is piece-stable by construction and can never become per-pixel noise.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float BreakupVariation = 0.25f;

	// 0 is an exact rendering identity, and the passes are skipped entirely rather than run to
	// reproduce their input.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float BreakupAmount = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (UIMin = "-1.0", UIMax = "1.0", Delta = "0.01"))
	float BreakupRoughnessAmount = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Shading", meta = (UIMin = "0.0", UIMax = "4.0", Delta = "0.05"))
	float BreakupNormalStrength = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Shading", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float BreakupNormalSharpness = 0.75f;



	// Flips the sign of the field, swapping which side of every boundary is the piece.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup")
	bool bBreakupInvert = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup", meta = (UIMin = "0", UIMax = "9999", Delta = "1"))
	int32 BreakupSeed = 1;

	// Optional mask owned by Breakup. When unset, the layer's accumulated mask children are used.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Placement")
	TSoftObjectPtr<UMixtormatMask> BreakupMask;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Placement")
	TSoftObjectPtr<UTexture2D> BreakupMaskTexture;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Placement", meta = (UIMin = "1", UIMax = "16", Delta = "1"))
	int32 BreakupMaskTiling = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Breakup|Placement")
	bool bBreakupInvertMask = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges", meta = (UIMin = "1", UIMax = "64", Delta = "1"))
	int32 EdgeWearRadius = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges", meta = (UIMin = "0.0", UIMax = "4.0", Delta = "0.01"))
	float EdgeWearSlope = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float EdgeWearStrength = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges", meta = (UIMin = "0.0", UIMax = "8.0", Delta = "0.01"))
	float EdgeWearFeather = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges", meta = (UIMin = "8", UIMax = "32", Delta = "1"))
	int32 EdgeWearDirections = 24;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float EdgeWearAngularAA = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float EdgeWearGravity = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges", meta = (UIMin = "-360.0", UIMax = "360.0", Delta = "1.0"))
	float EdgeWearGravityAngle = -90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges", meta = (UIMin = "0", UIMax = "1024", Delta = "1"))
	int32 EdgeWearSeed = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges", meta = (UIMin = "1", UIMax = "64", Delta = "1"))
	int32 EdgeWearMacroScale = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges", meta = (UIMin = "0.0", UIMax = "2.0", Delta = "0.01"))
	float EdgeWearMacroAmount = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges", meta = (UIMin = "1", UIMax = "64", Delta = "1"))
	int32 EdgeWearCellScale = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges", meta = (UIMin = "0.0", UIMax = "2.0", Delta = "0.01"))
	float EdgeWearCellAmount = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges", meta = (UIMin = "1", UIMax = "64", Delta = "1"))
	int32 EdgeWearRidgeScale = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges", meta = (UIMin = "0.0", UIMax = "2.0", Delta = "0.01"))
	float EdgeWearRidgeAmount = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges", meta = (UIMin = "1", UIMax = "128", Delta = "1"))
	int32 EdgeWearMicroScale = 40;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges", meta = (UIMin = "0.0", UIMax = "2.0", Delta = "0.01"))
	float EdgeWearMicroAmount = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges", meta = (UIMin = "1", UIMax = "64", Delta = "1"))
	int32 EdgeWearWarpScale = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges", meta = (UIMin = "0.0", UIMax = "2.0", Delta = "0.01"))
	float EdgeWearWarpAmount = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges", meta = (UIMin = "0.05", UIMax = "8.0", Delta = "0.01"))
	float EdgeWearNoiseContrast = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float EdgeWearIdVariation = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges", meta = (UIMin = "0.0", UIMax = "4.0", Delta = "0.01"))
	float EdgeWearIdRadius = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges", meta = (UIMin = "0.0", UIMax = "4.0", Delta = "0.01"))
	float EdgeWearIdSlope = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges", meta = (UIMin = "0.0", UIMax = "4.0", Delta = "0.01"))
	float EdgeWearIdStrength = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges", meta = (UIMin = "0.0", UIMax = "4.0", Delta = "0.01"))
	float EdgeWearIdNoise = 1.0f;

	// Roughness is applied only through the generated wear coverage. Weight is the output
	// enable/strength control; Offset is signed so worn edges may become rougher or smoother.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges|Output", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float EdgeWearRoughnessWeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worn Edges|Output", meta = (UIMin = "-1.0", UIMax = "1.0", Delta = "0.01"))
	float EdgeWearRoughnessOffset = 0.0f;

	// Tileable curl-flow distortion applied to every composited material channel together.
	// Amount is signed: reversing it follows the same field in the opposite direction.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow Warp", meta = (UIMin = "-4.0", UIMax = "4.0", Delta = "0.01"))
	float FlowWarpAmount = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow Warp", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float FlowWarpWeight = 1.0f;

	// Integer cells per UV repeat keep the generated vector field seamless.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow Warp", meta = (UIMin = "1", UIMax = "128", Delta = "1"))
	int32 FlowWarpScale = 8;

	// Rotates the curl vectors without rotating their periodic sampling lattice.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow Warp", meta = (UIMin = "-180.0", UIMax = "180.0", Delta = "1.0"))
	float FlowWarpDirection = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow Warp", meta = (UIMin = "0", UIMax = "1024", Delta = "1"))
	int32 FlowWarpSeed = 1;

	// Scoped-mask and current-height gradients steer the curl downhill. Zero preserves curl V1.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow Warp|Slope", meta = (UIMin = "0.0", UIMax = "4.0", Delta = "0.01"))
	float FlowWarpMaskSlopeInfluence = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow Warp|Slope", meta = (UIMin = "0.0", UIMax = "4.0", Delta = "0.01"))
	float FlowWarpHeightSlopeInfluence = 0.0f;

	// Pixel radii for the wrapped derivative kernel. Larger values reject finer slope detail.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow Warp|Slope", meta = (UIMin = "1.0", UIMax = "64.0", Delta = "1.0"))
	float FlowWarpDerivativeKernelX = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow Warp|Slope", meta = (UIMin = "1.0", UIMax = "64.0", Delta = "1.0"))
	float FlowWarpDerivativeKernelY = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow Warp|Output")
	EMixtormatFlowWarpBlendMode FlowWarpBlendMode = EMixtormatFlowWarpBlendMode::Replace;

	// ---- Layer Blur ------------------------------------------------------------------
	// Per axis, like the mask blur, and for the same reasons: the shader runs a dispatch per
	// direction so a zero radius costs nothing, an unequal pair is anisotropic, and each axis
	// can be driven on its own where a direction enum could not be driven at all.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Blur", meta = (DisplayName = "Radius X", UIMin = "0.0", UIMax = "32.0", Delta = "0.1"))
	float LayerBlurRadiusX = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Blur", meta = (DisplayName = "Radius Y", UIMin = "0.0", UIMax = "32.0", Delta = "0.1"))
	float LayerBlurRadiusY = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Blur")
	EMixtormatLayerBlurScope LayerBlurScope = EMixtormatLayerBlurScope::Layer;

	// Lerped against the unblurred source, so 0 is the identity and the pass is skipped
	// outright rather than paying for a dispatch that reproduces its own input.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Blur", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float LayerBlurAmount = 1.0f;

	// Height last, and optional, because it is the one channel where softening changes what
	// the surface *is* rather than how it looks: the height feeds displacement, the height
	// blend between layers, and the normals derived from it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layer Blur", meta = (DisplayName = "Blur Height"))
	bool bLayerBlurHeight = true;
};
