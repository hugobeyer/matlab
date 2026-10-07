// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UObject/SoftObjectPtr.h"
#include "MixtormatLayerTypes.h"
#include "MixtormatMaterial.generated.h"

class UMaterialInterface;
class UMixtormatSurface;
class UTexture2D;

USTRUCT(BlueprintType)
struct MIXTORMATRUNTIME_API FMixtormatFinalSettings
{
	GENERATED_BODY()

	// Ambient occlusion from the final height. AO is never written per layer or effect.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Final", meta = (UIMin = "0.0", UIMax = "1.0", Delta = "0.01"))
	float HeightAOAmount = 0.5f;

	// How far the occlusion reaches, in pixels at 1024 (scales with resolution).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Final", meta = (UIMin = "1.0", UIMax = "64.0", Delta = "0.5"))
	float HeightAORadius = 8.0f;

	// Retained for saved recipes. Final-height normals are now always enabled at unit strength.
	UPROPERTY()
	bool bNormalFromHeight = true;

	UPROPERTY()
	float HeightNormalStrength = 1.0f;

	// Remap the finished height to 0..1 from its own measured range, after every layer and
	// before final AO and normals. Layer heights are no longer clipped to 0..1 in the composite,
	// so this is the one place the document is brought back into range.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Final")
	bool bAutoRemapHeight = false;

	bool operator==(const FMixtormatFinalSettings& Other) const
	{
		return HeightAOAmount == Other.HeightAOAmount && HeightAORadius == Other.HeightAORadius
			&& bNormalFromHeight == Other.bNormalFromHeight
			&& HeightNormalStrength == Other.HeightNormalStrength
			&& bAutoRemapHeight == Other.bAutoRemapHeight;
	}
	bool operator!=(const FMixtormatFinalSettings& Other) const { return !(*this == Other); }
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

	// Passes on the finished surface (final height AO and normal).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Final")
	FMixtormatFinalSettings FinalSettings;
};
