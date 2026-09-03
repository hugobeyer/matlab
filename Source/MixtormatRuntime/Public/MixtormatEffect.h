#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MixtormatEffect.generated.h"

class UTexture2D;

// Surface effects write coverage, normal and AO through the effect data target.
// Filter effects transform one composited channel in place and are the identity at zero
// amount. Erosion is the first Filter; height blur and slope limiting would join it.
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
	Erosion = 2 UMETA(DisplayName = "Erosion")
};

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Textures", meta = (DisplayName = "Peel Data Map"))
	TObjectPtr<UTexture2D> PeelData;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Textures", meta = (DisplayName = "Coverage / Edge / Detail Mask"))
	TObjectPtr<UTexture2D> Mask;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Textures")
	TObjectPtr<UTexture2D> Height;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Textures", meta = (DisplayName = "Signed Distance Field"))
	TObjectPtr<UTexture2D> SDF;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Textures", meta = (DisplayName = "Bent Normal"))
	TObjectPtr<UTexture2D> BentNormal;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decode", meta = (ClampMin = "0.000001"))
	float DistanceRange = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decode", meta = (ClampMin = "0.000001"))
	float SDFRange = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Decode", meta = (ClampMin = "0.000001"))
	float HeightRange = 0.1f;

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stain Defaults")
	FLinearColor DefaultStainColor = FLinearColor(0.22f, 0.09f, 0.035f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stain Defaults", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float DefaultStainRoughness = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stain Defaults", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefaultStainHeightInfluence = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stain Defaults", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DefaultStainHeightWarp = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stain Defaults", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float DefaultStainHeightBias = -1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stain Defaults", meta = (ClampMin = "0.01"))
	float DefaultStainHeightContrast = 1.0f;
};
