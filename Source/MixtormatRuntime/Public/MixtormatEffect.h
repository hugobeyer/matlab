// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MixtormatEffect.generated.h"

class UTexture2D;

// Surface effects write coverage, normal and AO through the effect data target.
// Filter effects write no effect data. Erosion and Chipping carve height and Grade transforms
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
	Chipping = 4 UMETA(DisplayName = "Chipping"),
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
	case EMixtormatEffectType::Chipping:
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefaultFront = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults", meta = (ClampMin = "0.000001"))
	float DefaultWidth = 0.015f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults")
	float DefaultMacroWarp = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults")
	float DefaultMicroWarp = 0.003f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefaultMicroMorph = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults", meta = (ClampMin = "0.0"))
	float DefaultThickness = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults", meta = (ClampMin = "0.0"))
	float DefaultLift = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defaults", meta = (ClampMin = "0.0"))
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
