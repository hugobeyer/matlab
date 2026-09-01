#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MaterialLabSurface.generated.h"

class UMaterialInterface;
class UTexture2D;

UENUM(BlueprintType)
enum class EMaterialLabBlendHeightProvenance : uint8
{
	None = 0 UMETA(DisplayName = "No Spatial Height"),
	DerivedFromNormal = 1 UMETA(DisplayName = "Derived from Normal"),
	AuthoredRAMH = 2 UMETA(DisplayName = "Authored RAMH")
};

UCLASS(BlueprintType)
class MATERIALLABRUNTIME_API UMaterialLabSurface final : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual void PostLoad() override;
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "Identity")
	FName Family;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "Identity")
	FName Subtype;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "Identity")
	FName Finish;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "Identity")
	FName Structure = TEXT("Plain");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, AssetRegistrySearchable, Category = "Identity", meta = (ClampMin = "1"))
	int32 Variant = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Identity")
	FString SourceTextureBaseName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Textures")
	TObjectPtr<UTexture2D> BaseColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Textures")
	TObjectPtr<UTexture2D> Normal;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Textures", meta = (DisplayName = "Roughness / AO / Metallic / Optional Height"))
	TObjectPtr<UTexture2D> RoughnessAOMetallic;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Textures")
	bool bHasBlendHeight = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Textures")
	EMaterialLabBlendHeightProvenance BlendHeightProvenance =
		EMaterialLabBlendHeightProvenance::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Textures")
	FString DerivedHeightSourceHash;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface", meta = (ClampMin = "1.0", ClampMax = "3.0"))
	float DefaultIOR = 1.5f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Preview")
	TObjectPtr<UMaterialInterface> PreviewMaterial;
};
